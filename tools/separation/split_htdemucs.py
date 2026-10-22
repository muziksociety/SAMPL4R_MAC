"""Build models/htdemucs_body.onnx from models/htdemucs.onnx.

Why: the exported htdemucs.onnx carries the inverse STFT as one ConvTranspose (kernel 4096,
stride 1024, weights [4098,1,4096] = a brute-force inverse DFT). GPU execution providers
cannot run it (DirectML: out of memory on 3 GB cards / driver reset on Intel iGPUs) and it is
slow on CPU. The body model stops right before that node; OnnxSeparationProvider finishes the
inverse STFT on the host with an FFT (sm5::Fft / PFFFT):

    y = OLA_hop1024( irfft4096(X) * hann ) * 64 / sum_f hann^2(t - f*1024)
    out = time_branch + crop(y)          (64 = sqrt(4096): Demucs' normalised STFT)

Body outputs (names are fixed by the export and checked here):
    /Concat_18_output_0   [8, 4098, frames]  real bins 0..2048 then imag bins 0..2048
    /Add_7_output_0       [1, 4, 2, 343980]   time-branch output (added to the iSTFT)

Usage:  python tools/separation/split_htdemucs.py [in.onnx] [out_body.onnx]
Needs:  pip install onnx onnxruntime numpy
Verifies the body + an FFT tail against the full model on CPU before writing (>= 100 dB).
"""
import sys, os
import numpy as np
import onnx
from onnx import helper, shape_inference
import onnxruntime as ort

SPEC_OUT, TIME_OUT = '/Concat_18_output_0', '/Add_7_output_0'
N, K, HOP, SEG, CROP0, OUT_LEN = 4096, 2049, 1024, 343980, 1536, 347136


def build_body(src_path):
    m = shape_inference.infer_shapes(onnx.load(src_path))
    g = m.graph
    ct = [n for n in g.node if n.op_type == 'ConvTranspose'
          and any(a.name == 'kernel_shape' and list(a.ints) == [N] for a in n.attribute)]
    if len(ct) != 1:
        raise SystemExit(f'expected exactly one iSTFT ConvTranspose (kernel {N}), found {len(ct)}')
    ct = ct[0]
    if ct.input[0] != SPEC_OUT:
        raise SystemExit(f'iSTFT input is {ct.input[0]}, expected {SPEC_OUT}')
    cons = {}
    for n in g.node:
        for i in n.input:
            cons.setdefault(i, []).append(n)
    tail, stack = {ct.name}, [ct]
    while stack:
        n = stack.pop()
        for o in n.output:
            for c in cons.get(o, []):
                if c.name not in tail:
                    tail.add(c.name); stack.append(c)
    if not any(n.op_type == 'Add' and TIME_OUT in n.input for n in g.node if n.name in tail):
        raise SystemExit(f'tail does not add {TIME_OUT}; export layout changed')
    keep = [n for n in g.node if n.name not in tail]
    vi = {v.name: v for v in list(g.value_info) + list(g.input) + list(g.output)}
    outs = [vi[SPEC_OUT], vi[TIME_OUT]]
    body = helper.make_graph(keep, 'htdemucs_body', list(g.input), outs, list(g.initializer))
    bm = helper.make_model(body, opset_imports=m.opset_import)
    bm.ir_version = m.ir_version
    # drop nodes/initializers that no longer feed an output
    bm = onnx.utils.Extractor(bm).extract_model(['mix'], [SPEC_OUT, TIME_OUT])
    return bm


def host_tail(spec, time_branch):
    """The C++ tail (OnnxSeparationProvider) in numpy, for verification."""
    n = np.arange(N); hann = 0.5 - 0.5 * np.cos(2 * np.pi * n / N)
    b, _, frames = spec.shape
    length = (frames - 1) * HOP + N
    env = np.zeros(length)
    for f in range(frames):
        env[f * HOP:f * HOP + N] += hann ** 2
    fr = np.fft.irfft(spec[:, :K, :].astype(np.float64) + 1j * spec[:, K:, :].astype(np.float64), n=N, axis=1) * hann[None, :, None]
    y = np.zeros((b, length))
    for f in range(frames):
        y[:, f * HOP:f * HOP + N] += fr[:, :, f]
    y = y[:, 2048:2048 + OUT_LEN] * (64.0 / env[2048:2048 + OUT_LEN])
    y = y.reshape(1, 4, 2, OUT_LEN)[..., CROP0:CROP0 + SEG]
    return (time_branch.astype(np.float64) + y).astype(np.float32)


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else 'models/htdemucs.onnx'
    dst = sys.argv[2] if len(sys.argv) > 2 else 'models/htdemucs_body.onnx'
    body = build_body(src)
    onnx.save(body, dst)
    so = ort.SessionOptions(); so.log_severity_level = 3
    full = ort.InferenceSession(src, so, providers=['CPUExecutionProvider'])
    part = ort.InferenceSession(dst, so, providers=['CPUExecutionProvider'])
    x = np.random.default_rng(7).standard_normal((1, 2, SEG)).astype(np.float32) * 0.2
    ref = full.run(None, {'mix': x})[0]
    spec, tb = part.run([SPEC_OUT, TIME_OUT], {'mix': x})
    got = host_tail(spec, tb)
    snr = 10 * np.log10(np.sum(ref.astype(np.float64) ** 2) / np.sum((ref.astype(np.float64) - got) ** 2))
    print(f'{dst}: {len(body.graph.node)} nodes, {os.path.getsize(dst) / 2**20:.1f} MB, body+host tail vs full = {snr:.1f} dB')
    if snr < 100:
        os.remove(dst)
        raise SystemExit('verification FAILED (< 100 dB): body not written')


if __name__ == '__main__':
    main()
