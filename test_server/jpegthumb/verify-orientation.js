/* Confirms the browser honours the APP1 orientation that nxthumb injects.
 *
 * A portrait NX500 frame stores its pixels sideways and only records the
 * rotation as an EXIF tag. The slicer prepends a 36-byte APP1 carrying just
 * that tag; if the browser ignores it the grid would show portrait shots lying
 * on their side, which the old ImageMagick path avoided via -auto-orient.
 *
 * Run with NODE_PATH pointing at the directory containing playwright-core:
 *   NODE_PATH=... node test_server/jpegthumb/verify-orientation.js
 */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const http = require('node:http');
const path = require('node:path');
const { chromium } = require('playwright-core');

const FIXTURES = process.env.NXTHUMB_FIXTURES ||
    path.join(__dirname, '../../../.tmp/orient_test');
const EDGE = process.env.EDGE_PATH || 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe';

const TYPES = { '.jpg': 'image/jpeg', '.jpeg': 'image/jpeg' };

function serve() {
    const server = http.createServer((req, res) => {
        const name = decodeURIComponent(req.url.replace(/^\//, '')) || 'index.html';
        if (name === 'index.html') {
            res.writeHead(200, { 'Content-Type': 'text/html' });
            res.end('<!doctype html><body style="margin:0"></body>');
            return;
        }
        const file = path.join(FIXTURES, path.basename(name));
        if (!fs.existsSync(file)) {
            res.writeHead(404);
            res.end('missing');
            return;
        }
        res.writeHead(200, { 'Content-Type': TYPES[path.extname(file)] || 'application/octet-stream' });
        res.end(fs.readFileSync(file));
    });
    return new Promise(resolve => {
        server.listen(0, '127.0.0.1', () => resolve(server));
    });
}

async function measure(page, src) {
    return page.evaluate(async url => {
        const img = new Image();
        img.src = url;
        try {
            await img.decode();
        } catch (e) {
            return { error: String(e) };
        }
        document.body.appendChild(img);
        const rect = img.getBoundingClientRect();
        return {
            natural: [img.naturalWidth, img.naturalHeight],
            rendered: [Math.round(rect.width), Math.round(rect.height)],
            orientation: getComputedStyle(img).imageOrientation
        };
    }, src);
}

async function main() {
    for (const name of ['landscape.jpg', 'portrait.jpg']) {
        if (!fs.existsSync(path.join(FIXTURES, name))) {
            console.error('missing fixture ' + path.join(FIXTURES, name));
            return 1;
        }
    }
    const server = await serve();
    const base = 'http://127.0.0.1:' + server.address().port + '/';
    const browser = await chromium.launch({ executablePath: EDGE, headless: true });
    try {
        const page = await browser.newPage({ viewport: { width: 900, height: 700 } });
        await page.goto(base);

        const landscape = await measure(page, base + 'landscape.jpg');
        const portrait = await measure(page, base + 'portrait.jpg');
        console.log('landscape (orientation=1):', JSON.stringify(landscape));
        console.log('portrait  (orientation=8):', JSON.stringify(portrait));

        assert.ok(!landscape.error, 'landscape failed to decode: ' + landscape.error);
        assert.ok(!portrait.error, 'portrait failed to decode: ' + portrait.error);

        // Landscape keeps the stored 320x216.
        assert.deepEqual(landscape.natural, [320, 216], 'landscape natural size');

        // Portrait must come out tall: the injected tag has to be applied, so
        // the intrinsic size swaps to 216x320.
        assert.deepEqual(portrait.natural, [216, 320],
            'portrait natural size should be rotated by the injected APP1');
        assert.ok(portrait.rendered[1] > portrait.rendered[0],
            'portrait should render taller than wide');

        console.log('\nPASS: browser rotates on the injected orientation tag');
        return 0;
    } finally {
        await browser.close();
        server.close();
    }
}

main().then(code => process.exit(code)).catch(err => {
    console.error('FAIL:', err.message);
    process.exit(1);
});
