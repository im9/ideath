const vscode = require('vscode');
const net = require('net');

const PORT = 7777;
const HOST = '127.0.0.1';

function send(text) {
    return new Promise((resolve, reject) => {
        const sock = new net.Socket();
        sock.connect(PORT, HOST, () => {
            sock.end(text + '\n');
            resolve();
        });
        sock.on('error', (err) => {
            reject(err);
        });
    });
}

function activate(context) {
    const cmd = vscode.commands.registerCommand('ideath.send', async () => {
        const editor = vscode.window.activeTextEditor;
        if (!editor) return;

        const sel = editor.selection;
        let text;

        if (sel.isEmpty) {
            // No selection — send current line
            text = editor.document.lineAt(sel.active.line).text;
        } else {
            text = editor.document.getText(sel);
        }

        text = text.trim();
        if (!text) return;

        try {
            await send(text);
            vscode.window.setStatusBarMessage(`iDEATH ← ${text.split('\n')[0]}`, 2000);
        } catch {
            vscode.window.showWarningMessage('iDEATH REPL not running (127.0.0.1:7777)');
        }
    });

    context.subscriptions.push(cmd);
}

function deactivate() {}

module.exports = { activate, deactivate };
