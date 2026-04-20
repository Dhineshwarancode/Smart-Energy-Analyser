const http = require('http');
const fs = require('fs');
const path = require('path');

const ESP32_IP = '10.233.195.172';
const PORT = 3000;

const MIME_TYPES = {
    '.html': 'text/html',
    '.css': 'text/css',
    '.js': 'application/javascript',
    '.json': 'application/json',
    '.png': 'image/png',
    '.jpg': 'image/jpeg',
    '.svg': 'image/svg+xml',
    '.ico': 'image/x-icon',
};

const PROXY_PATHS = ['/data', '/on', '/off', '/recharge', '/relay', '/threshold'];

async function proxyToESP32(req, res) {
    const url = `http://${ESP32_IP}${req.url}`;
    console.log(`[PROXY] ${req.method} ${req.url} -> ${url}`);

    try {
        const response = await fetch(url, {
            method: req.method,
            headers: {
                'User-Agent': req.headers['user-agent'] || 'Mozilla/5.0',
                'Connection': 'close'
            },
            signal: AbortSignal.timeout(5000)
        });

        const headers = {};
        for (const [key, value] of response.headers.entries()) {
            headers[key] = value;
        }

        res.writeHead(response.status, {
            ...headers,
            'Access-Control-Allow-Origin': '*',
            'Access-Control-Allow-Methods': 'GET, POST, OPTIONS, PUT',
            'Access-Control-Allow-Headers': 'Content-Type',
        });

        const arrayBuffer = await response.arrayBuffer();
        res.end(Buffer.from(arrayBuffer));

    } catch (err) {
        console.error(`[PROXY ERROR] ${err.message}`);
        if (!res.headersSent) {
            const status = err.name === 'TimeoutError' ? 504 : 502;
            res.writeHead(status, { 'Content-Type': 'application/json', 'Access-Control-Allow-Origin': '*' });
            res.end(JSON.stringify({ error: err.message === 'TimeoutError' ? 'ESP32 timeout' : 'ESP32 unreachable', details: err.message }));
        }
    }
}

function serveStaticFile(req, res) {
    let filePath = path.join(__dirname, req.url === '/' ? 'index.html' : req.url);
    const ext = path.extname(filePath);
    const contentType = MIME_TYPES[ext] || 'application/octet-stream';

    fs.readFile(filePath, (err, data) => {
        if (err) {
            res.writeHead(404, { 'Content-Type': 'text/plain' });
            res.end('404 Not Found');
            return;
        }
        res.writeHead(200, { 'Content-Type': contentType });
        res.end(data);
    });
}

const server = http.createServer((req, res) => {
    // Handle CORS preflight
    if (req.method === 'OPTIONS') {
        res.writeHead(204, {
            'Access-Control-Allow-Origin': '*',
            'Access-Control-Allow-Methods': 'GET, POST, OPTIONS',
            'Access-Control-Allow-Headers': 'Content-Type',
        });
        res.end();
        return;
    }

    // Check if this is an API path that should go to ESP32
    const urlPath = req.url.split('?')[0];
    if (PROXY_PATHS.includes(urlPath)) {
        proxyToESP32(req, res);
    } else {
        serveStaticFile(req, res);
    }
});

server.listen(PORT, () => {
    console.log(`\n  ⚡ Energy Dashboard Server running at http://localhost:${PORT}`);
    console.log(`  📡 Proxying API requests to ESP32 at http://${ESP32_IP}`);
    console.log(`  🔗 Proxy routes: ${PROXY_PATHS.join(', ')}\n`);
});
