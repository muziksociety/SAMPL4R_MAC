const fs = require('fs');
const path = require('path');

const gameFiles = [
    'src/ui/Games/BlockPuzzleGame.cpp',
    'src/ui/Games/MiniGameHost.cpp',
    'src/ui/Games/SnakeGame.cpp',
    'src/ui/Games/FlappyGame.cpp',
    'src/ui/Games/Game2048.cpp',
    'src/ui/Games/BreakoutGame.cpp',
    'src/ui/Games/SpaceShooterGame.cpp',
    'src/ui/Games/DummyGame.cpp'
];

let totalRemoved = 0;

gameFiles.forEach(file => {
    if (!fs.existsSync(file)) {
        console.log(`  ${file}: File not found`);
        return;
    }
    
    const content = fs.readFileSync(file, 'utf8');
    const lines = content.split('\n');
    
    const logLines = lines.filter(line => line.includes('juce::Logger::writeToLog'));
    const count = logLines.length;
    
    if (count > 0) {
        const newLines = lines.filter(line => !line.includes('juce::Logger::writeToLog'));
        fs.writeFileSync(file, newLines.join('\n'), 'utf8');
        console.log(`✓ ${file}: Removed ${count} log lines`);
        totalRemoved += count;
    } else {
        console.log(`  ${file}: No log lines found`);
    }
});

console.log(`\n✅ Total: Removed ${totalRemoved} log lines from game files`);

