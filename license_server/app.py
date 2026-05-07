"""
Sample Monsta License Server (FastAPI)
- Provides production-compatible routes
- Adds legacy/local-compatible routes so nothing 404s
- Minimal in-memory data to validate/demo
"""
from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse, HTMLResponse, RedirectResponse
from pydantic import BaseModel
from datetime import datetime
from typing import List, Dict

app = FastAPI(title="Sample Monsta License Server", version="1.0.0")

# In-memory "DB" for demo
licenses_db: Dict[str, Dict] = {
    "SM5-DEBUG-PANCHO": {
        "email": "panchovilla@gmail.com",
        "license_type": "ENT",
        "customer_name": "Debug User",
        "max_activations": 3,
        "activations": [],
    },
    "SM5-TEST-12345": {
        "email": "test@example.com",
        "license_type": "PRO",
        "customer_name": "Test User",
        "max_activations": 2,
        "activations": [],
    },
}
users_db: Dict[str, Dict] = {
    "panchovilla@gmail.com": {
        "password": "panchovilla..",
        "licenses": ["SM5-DEBUG-PANCHO"],
    },
    "test@example.com": {
        "password": "test123",
        "licenses": ["SM5-TEST-12345"],
    },
}

# Models (loose validation for compatibility)
class LicenseValidationRequest(BaseModel):
    license_key: str
    hardware_id: str

class AuthRequest(BaseModel):
    email: str
    password: str
    hardware_id: str | None = None

# ---------------------
# Health & Info
# ---------------------
@app.get("/health")
async def health():
    return {"status": "healthy", "timestamp": datetime.utcnow().isoformat()}

@app.get("/api/v1/health")
async def health_v1():
    return await health()

@app.get("/api/health")
async def health_legacy():
    # Keep for full compatibility with older clients
    return await health()

@app.get("/info")
async def info():
    return {
        "server": {"title": "Sample Monsta License Server", "version": "1.0.0", "debug": False},
        "services": ["license_api", "admin_interface", "portal", "monitoring"],
        "timestamp": datetime.utcnow().isoformat(),
    }

# ---------------------
# HTML Pages
# ---------------------
HOME_HTML = """
<!doctype html><html><head><title>Sample Monsta - License Server</title></head>
<body style='font-family: Arial; padding: 20px;'>
  <h1>🔐 Sample Monsta License Server</h1>
  <p>This server provides license validation/activation APIs and a simple UI.</p>
  <h3>Pages</h3>
  <ul>
    <li><a href="/">Home</a></li>
    <li><a href="/portal/">Portal</a></li>
    <li><a href="/admin">Admin</a></li>
    <li><a href="/login">Login</a></li>
    <li><a href="/register">Register</a></li>
  </ul>
  <h3>Health</h3>
  <ul>
    <li><a href="/health">/health</a></li>
    <li><a href="/api/v1/health">/api/v1/health</a></li>
    <li><a href="/api/health">/api/health</a></li>
  </ul>
  <h3>API (JSON)</h3>
  <ul>
    <li>POST /api/validate, /api/v1/validate</li>
    <li>POST /api/activate</li>
    <li>POST /api/license/validate (legacy)</li>
    <li>POST /api/license/activate (legacy)</li>
    <li>POST /api/license/authenticate (legacy)</li>
    <li>POST /auth/login (legacy)</li>
    <li>POST /auth/login-with-hardware (legacy)</li>
  </ul>
  <p>See <a href="/docs">/docs</a> for API docs.</p>
</body></html>
"""

PORTAL_HTML = """
<!doctype html><html><head><title>Portal</title></head>
<body style='font-family: Arial; padding: 20px;'>
  <h1>👤 User Portal</h1>
  <p>Demo user: panchovilla@gmail.com</p>
  <h3>Licenses</h3>
  <ul>
    <li>SM5-DEBUG-PANCHO (ENT)</li>
  </ul>
  <a href="/">← Back</a>
</body></html>
"""

ADMIN_HTML = """
<!doctype html><html><head><title>Admin</title></head>
<body style='font-family: Arial; padding: 20px;'>
  <h1>⚙️ Admin Panel</h1>
  <p>Simple read-only list of licenses (demo)</p>
  <table border="1" cellpadding="6" cellspacing="0">
    <tr><th>License Key</th><th>Email</th><th>Type</th><th>Activations</th></tr>
    {rows}
  </table>
  <a href="/">← Back</a>
</body></html>
"""

LOGIN_HTML = """
<!doctype html><html><head><title>Login</title></head>
<body style='font-family: Arial; padding: 20px;'>
  <h1>Login</h1>
  <p>This is a placeholder login page for production. For API-based login, use POST /auth/login.</p>
  <form method="post" action="/auth/login" onsubmit="return false;">
    <label>Email <input name="email" value="panchovilla@gmail.com"></label><br/>
    <label>Password <input name="password" type="password" value="panchovilla.."></label><br/>
    <p>Use API client to POST JSON to /auth/login.</p>
  </form>
  <a href="/">← Back</a>
</body></html>
"""

REGISTER_HTML = """
<!doctype html><html><head><title>Register</title></head>
<body style='font-family: Arial; padding: 20px;'>
  <h1>Register</h1>
  <p>Placeholder registration page.</p>
  <a href="/">← Back</a>
</body></html>
"""

@app.get("/", response_class=HTMLResponse)
async def home_page():
    return HTMLResponse(HOME_HTML)

@app.get("/portal/", response_class=HTMLResponse)
@app.get("/portal", response_class=HTMLResponse)
async def portal_page():
    return HTMLResponse(PORTAL_HTML)

@app.get("/admin", response_class=HTMLResponse)
async def admin_page():
    rows = []
    for key, data in licenses_db.items():
        rows.append(f"<tr><td>{key}</td><td>{data['email']}</td><td>{data['license_type']}</td><td>{len(data['activations'])}/{data['max_activations']}</td></tr>")
    html = ADMIN_HTML.replace("{rows}", "\n".join(rows))
    return HTMLResponse(html)

@app.get("/login", response_class=HTMLResponse)
async def login_page():
    return HTMLResponse(LOGIN_HTML)

@app.get("/register", response_class=HTMLResponse)
async def register_page():
    return HTMLResponse(REGISTER_HTML)

# ---------------------
# API - license (production-style)
# ---------------------
@app.post("/api/validate")
@app.post("/api/v1/validate")
async def api_validate(req: LicenseValidationRequest):
    lic = licenses_db.get(req.license_key)
    if not lic:
        return JSONResponse({"valid": False, "message": "Invalid license key"}, status_code=400)
    return {
        "valid": True,
        "message": "License validated successfully",
        "license_info": {
            "license_key": req.license_key,
            "license_type": lic["license_type"],
            "customer_name": lic["customer_name"],
            "email": lic["email"],
        },
        "validation_info": {"validated_at": datetime.utcnow().isoformat(), "hardware_id": req.hardware_id},
    }

@app.post("/api/activate")
async def api_activate(req: LicenseValidationRequest):
    lic = licenses_db.get(req.license_key)
    if not lic:
        return JSONResponse({"valid": False, "message": "Invalid license key"}, status_code=400)
    if len(lic["activations"]) >= lic["max_activations"]:
        return JSONResponse({"valid": False, "message": "Maximum activations reached"}, status_code=400)
    if not any(a["hardware_id"] == req.hardware_id for a in lic["activations"]):
        lic["activations"].append({"hardware_id": req.hardware_id, "activated_at": datetime.utcnow().isoformat()})
    return {"valid": True, "message": "License activated successfully", "license_info": {"license_key": req.license_key, "license_type": lic["license_type"], "customer_name": lic["customer_name"]}}

# ---------------------
# API - legacy/local compatibility
# ---------------------
@app.post("/api/license/validate")
async def legacy_validate(req: LicenseValidationRequest):
    return await api_validate(req)

@app.post("/api/license/activate")
async def legacy_activate(req: LicenseValidationRequest):
    return await api_activate(req)

@app.post("/api/license/authenticate")
async def legacy_authenticate(req: AuthRequest):
    user = users_db.get(req.email)
    if user and user["password"] == req.password:
        return {"valid": True, "message": "Authentication successful", "license_info": {"email": req.email, "licenses": user["licenses"]}}
    return JSONResponse({"valid": False, "message": "Invalid credentials"}, status_code=401)

@app.post("/auth/login")
async def auth_login(req: AuthRequest):
    user = users_db.get(req.email)
    if user and user["password"] == req.password:
        return {"valid": True, "message": "Login successful", "license_info": {"email": req.email, "licenses": user["licenses"]}}
    return JSONResponse({"valid": False, "message": "Invalid credentials"}, status_code=401)

@app.post("/auth/login-with-hardware")
async def auth_login_hw(req: AuthRequest):
    user = users_db.get(req.email)
    if user and user["password"] == req.password:
        if user["licenses"]:
            license_key = user["licenses"][0]
            lic = licenses_db[license_key]
            if req.hardware_id and not any(a["hardware_id"] == req.hardware_id for a in lic["activations"]):
                if len(lic["activations"]) < lic["max_activations"]:
                    lic["activations"].append({"hardware_id": req.hardware_id, "activated_at": datetime.utcnow().isoformat()})
            return {"valid": True, "message": "Login and activation successful", "license_info": {"license_key": license_key, "license_type": lic["license_type"], "customer_name": lic["customer_name"], "email": req.email}}
    return JSONResponse({"valid": False, "message": "Invalid credentials"}, status_code=401)

# Root redirect for /dashboard -> /portal
@app.get("/dashboard")
async def dashboard_redirect():
    return RedirectResponse(url="/portal/")

# Uvicorn entrypoint
# Run with: uvicorn license_server.app:app --host 0.0.0.0 --port 8000

