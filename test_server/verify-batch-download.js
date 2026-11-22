const { chromium } = require('playwright-core');
const path = require('path');
const assert = require('assert');
const root = path.join(__dirname, '../scripts/nx-rc/web_root');
(async () => {
    const browser = await chromium.launch({executablePath: 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe', headless: true});
    try {
        const page = await browser.newPage({acceptDownloads: true});
        let attempts = 0;
        await page.route('http://127.0.0.1:8082/**', route => {
            const url = route.request().url();
            if (url.endsWith('/B.JPG') && ++attempts <= 3) return route.fulfill({status: 503});
            if (url.includes('/DCIM/')) return route.fulfill({status: 200, body: 'photo'});
            return route.fulfill({status: 200, contentType: 'text/html', body: '<div id="gallery"></div>'});
        });
        await page.goto('http://127.0.0.1:8082/');
        await page.addScriptTag({path: path.join(root, 'lib/jquery.min.js')});
        await page.addScriptTag({path: path.join(root, 'js/gallery.js')});
        await page.evaluate(() => {
            window.g = new Gallery({});
            g.media = ['A.JPG', 'B.JPG'].map(name => ({name, dir: '100PHOTO', url: 'http://127.0.0.1:8082/DCIM/100PHOTO/' + name}));
            g.selection = {0: true, 1: true};
            $('#gallery').append(g.buildToolbar());
            g.downloadSelected();
        });
        await page.waitForFunction(() => !g.downloading, null, {timeout: 12000});
        assert.equal(attempts, 3);
        assert.equal(await page.evaluate(() => g._failedDownloads.length), 1);
        await page.getByRole('button', {name: /重试失败/}).click();
        await page.waitForFunction(() => !g.downloading, null, {timeout: 5000});
        assert.equal(await page.evaluate(() => g._failedDownloads.length), 0);
        assert.equal(attempts, 4);
        await page.route('**/SLOW.JPG', async route => {
            await new Promise(resolve => setTimeout(resolve, 2500));
            await route.fulfill({status: 200, body: 'photo'}).catch(() => {});
        });
        await page.evaluate(() => {
            g.media = [{name: 'SLOW.JPG', dir: '100PHOTO', url: 'http://127.0.0.1:8082/DCIM/100PHOTO/SLOW.JPG'}];
            g.selection = {0: true};
            g.downloadSelected();
        });
        await page.getByRole('button', {name: '停止下载'}).click();
        await page.waitForFunction(() => !g.downloading, null, {timeout: 5000});
        assert.equal(await page.evaluate(() => g._failedDownloads.length), 1);
        console.log('PASS: serial batch, retry limit, retry failed, cancel');
    } finally { await browser.close(); }
})().catch(e => {console.error(e); process.exitCode = 1;});
