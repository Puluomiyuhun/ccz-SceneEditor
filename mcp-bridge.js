#!/usr/bin/env node
// cczEditor2 MCP Bridge - stdio <-> HTTP bridge
// Claude Code 通过 stdio 与此脚本通信，脚本转发请求到编辑器的 /mcp 端点
const http = require('http');
const readline = require('readline');

const PORT = process.env.CCZ_EDITOR_PORT || 8080;
const HOST = process.env.CCZ_EDITOR_HOST || '127.0.0.1';

const rl = readline.createInterface({ input: process.stdin, terminal: false });

function sendToEditor(body) {
  return new Promise((resolve, reject) => {
    const data = JSON.stringify(body);
    const req = http.request({
      hostname: HOST, port: PORT, path: '/mcp',
      method: 'POST',
      headers: { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(data) }
    }, (res) => {
      let chunks = '';
      res.on('data', c => chunks += c);
      res.on('end', () => {
        try { resolve(JSON.parse(chunks)); }
        catch (e) { reject(new Error('Invalid JSON from editor: ' + chunks.slice(0, 200))); }
      });
    });
    req.on('error', reject);
    req.write(data);
    req.end();
  });
}

function send(obj) {
  const s = JSON.stringify(obj);
  process.stdout.write(`Content-Length: ${Buffer.byteLength(s)}\r\n\r\n${s}`);
}

// 读取 JSON-RPC messages (Content-Length 分帧)
let buffer = '';
process.stdin.on('data', (chunk) => {
  buffer += chunk.toString();
  while (true) {
    const headerEnd = buffer.indexOf('\r\n\r\n');
    if (headerEnd === -1) break;
    const header = buffer.slice(0, headerEnd);
    const match = header.match(/Content-Length:\s*(\d+)/i);
    if (!match) { buffer = buffer.slice(headerEnd + 4); continue; }
    const len = parseInt(match[1]);
    const bodyStart = headerEnd + 4;
    if (buffer.length < bodyStart + len) break;
    const body = buffer.slice(bodyStart, bodyStart + len);
    buffer = buffer.slice(bodyStart + len);
    handleMessage(body);
  }
});

async function handleMessage(raw) {
  let msg;
  try { msg = JSON.parse(raw); } catch { return; }

  // Notifications don't need response
  if (!msg.id && msg.id !== 0) {
    sendToEditor(msg).catch(() => {});
    return;
  }

  try {
    const resp = await sendToEditor(msg);
    send(resp);
  } catch (e) {
    send({
      jsonrpc: '2.0', id: msg.id,
      error: { code: -32000, message: `编辑器连接失败(${HOST}:${PORT}): ${e.message}` }
    });
  }
}

process.stderr.write(`cczEditor2 MCP bridge started (target: ${HOST}:${PORT})\n`);
