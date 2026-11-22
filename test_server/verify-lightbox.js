/* Run with NODE_PATH pointing to the directory containing playwright-core. */
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');
const { chromium } = require('playwright-core');

const WEB_ROOT = path.join(__dirname, '../scripts/nx-rc/web_root');
const BASE = 'http://127.0.0.1:8082';
const CAMERA = 'http://127.0.0.1:8081';
const PIXEL = Buffer.from('R0lGODlhAQABAAD/ACwAAAAAAQABAAACADs=', 'base64');
const EDGE = process.env.EDGE_PATH || 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe';

async function verify() {
    const browser = await chromium.launch({ executablePath: EDGE, headless: true });
    try {
        const page = await browser.newPage();
        const fullRequests = [];
        const originals = [];
        let releasePreview;
        const heldPreview = new Promise(resolve => { releasePreview = resolve; });
        const pageErrors = [];
        page.on('pageerror', error => pageErrors.push(error.message));
        await page.context().route(/^http:\/\/127\.0\.0\.1:808[012]\//, async route => {
            const url = new URL(route.request().url());
            if (url.pathname === '/') {
                return route.fulfill({ contentType: 'text/html', body: '<div id="gallery"></div>' });
            }
            if (url.pathname === '/cgi-bin/thumb' && url.searchParams.get('w') === '1024') {
                const name = url.searchParams.get('f');
                fullRequests.push(name);
                if (name === '100PHOTO/B.JPG') {
                    await heldPreview;
                }
                if (name === '100PHOTO/C.JPG') {
                    return route.fulfill({ status: 503 });
                }
            } else if (url.pathname.startsWith('/DCIM/')) {
                originals.push(path.posix.basename(url.pathname));
            } else if (url.pathname === '/cgi-bin/download') {
                assert.equal(url.port, '8080');
                assert.equal(url.searchParams.get('f'), '100PHOTO/B.JPG');
                return route.fulfill({
                    headers: { 'Content-Disposition': 'attachment; filename="100PHOTO_B.JPG"' },
                    contentType: 'application/octet-stream',
                    body: PIXEL
                });
            }
            return route.fulfill({ contentType: 'image/gif', body: PIXEL });
        });
        await page.goto(BASE);
        await page.addStyleTag({ path: path.join(WEB_ROOT, 'css/style.css') });
        await page.addStyleTag({ path: path.join(WEB_ROOT, 'lib/font-awesome.min.css') });
        await page.addScriptTag({ path: path.join(WEB_ROOT, 'lib/jquery.min.js') });
        await page.addScriptTag({ path: path.join(WEB_ROOT, 'js/gallery.js') });
        await page.evaluate(camera => {
            const gallery = new Gallery({});
            gallery._ioSupported = false;
            gallery.media = ['A.JPG', 'B.JPG', 'C.JPG'].map(name => ({
                name, url: camera + '/DCIM/100PHOTO/' + name, type: 'image', dir: '100PHOTO'
            }));
            gallery.media.forEach((file, index) => {
                $('#gallery').append(gallery.buildItem({ file, index }));
            });
        }, CAMERA);
        await page.waitForFunction(() => [...document.querySelectorAll('#gallery img')]
            .every(img => img.complete && img.naturalWidth > 0));

        await page.locator('.gallery-item').nth(1).click();
        await page.waitForFunction(() => document.querySelector('.gallery-lb-image').src.includes('w=1024'));
        await page.waitForFunction(() => {
            const preview = document.querySelector('.gallery-lb-preview');
            return preview && preview.naturalWidth > 0 && preview.getClientRects().length;
        }, null, { timeout: 2500 });
        assert.deepEqual(fullRequests, ['100PHOTO/B.JPG']);
        assert.deepEqual(originals, []);

        releasePreview();
        const photo = page.locator('.gallery-lb-image');
        await photo.waitFor({ state: 'visible' });
        assert.equal(await page.locator('.gallery-lb-preview').isVisible(), false);
        await page.getByRole('button', { name: '查看原图' }).click();
        await page.waitForFunction(() => document.querySelector('.gallery-lb-image').currentSrc.endsWith('/DCIM/100PHOTO/B.JPG'));
        assert.deepEqual(originals, ['B.JPG']);

        const [download] = await Promise.all([
            page.waitForEvent('download', { timeout: 4000 }),
            page.locator('.gallery-lb-dl').click()
        ]);
        assert.equal(download.suggestedFilename(), '100PHOTO_B.JPG');
        assert.deepEqual(await fs.readFile(await download.path()), PIXEL);
        assert.deepEqual(originals, ['B.JPG']);

        await page.keyboard.press('ArrowRight');
        await page.waitForFunction(() => document.querySelector('.gallery-lb-image').currentSrc.endsWith('/DCIM/100PHOTO/C.JPG'));
        assert.deepEqual(fullRequests, ['100PHOTO/B.JPG', '100PHOTO/C.JPG']);
        assert.deepEqual(originals, ['B.JPG', 'C.JPG']);
        await page.keyboard.press('Escape');
        assert.equal(await page.locator('#galleryLightbox').isVisible(), false);
        assert.deepEqual(pageErrors, []);
        console.log('PASS: grid preview, original, cross-origin download, navigation, fallback, close');
    } finally {
        await browser.close();
    }
}

verify().catch(error => {
    console.error(error);
    process.exitCode = 1;
});
