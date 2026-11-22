const fs = require('fs');
const path = require('path');
const assert = require('assert');
const dir = __dirname;
const source = 'E:/新建文件夹/NX500_opensource_2015_03_04/home/songha.choi/osp/NX500/linux-3.5/drivers/net/wireless/sd8887/wlan_src';
const raw = fs.readFileSync(path.join(dir, 'PWRTABLE-RAW.txt'), 'utf8');
const results = [];
for (const section of raw.split(/^##### /m).slice(1)) {
    const name = section.split(/\r?\n/)[0];
    const meta = section.match(/Size=(0x[0-9a-f]+),.*Result=([0-9a-fx]+)/i);
    const dump = section.match(/payload: len=(\d+)\r?\n([\s\S]*?)\r?\n\s*rc=(\d+)/);
    assert(meta && dump, name);
    assert.equal(meta[2], '0000');
    assert.equal(Number(dump[3]), 0);
    const bytes = Buffer.from(dump[2].trim().split(/\s+/).map(x => {assert(/^[0-9a-f]{2}$/i.test(x)); return parseInt(x, 16);}));
    assert.equal(bytes.length, Number(dump[1]));
    assert.equal(parseInt(meta[1], 16), bytes.length + 8);
    assert.equal(bytes.readUInt16LE(0), 0);
    const entries = [];
    for (let i = 4; i < bytes.length;) {
        assert(i + 8 <= bytes.length);
        const type = bytes.readUInt16LE(i), length = bytes.readUInt16LE(i + 2);
        assert.equal(type, 0x189);
        assert(length >= 4 && i + length + 4 <= bytes.length && (length - 4) % 2 === 0);
        const rates = [];
        for (let j = i + 8; j < i + length + 4; j += 2) rates.push([bytes[j], bytes.readInt8(j + 1)]);
        entries.push({baseMHz: bytes.readUInt16LE(i + 4), width: bytes[i + 6], channel: bytes[i + 7], rates, rawHex: bytes.subarray(i, i + length + 4).toString('hex')});
        i += length + 4;
    }
    results.push({name, subband: bytes.readUInt16LE(2), hostcmdSize: parseInt(meta[1], 16), payloadBytes: bytes.length, result: meta[2], entries});
}
const reference = fs.readFileSync(path.join(source, 'mapp/mlanconfig/config/txpwrlimit_cfg.conf'), 'utf8');
const referenceSub3 = reference.split('txpwrlimit_5g_cfg_set_sub3={')[1];
const referencePairs = [...referenceSub3.matchAll(/TLVStartFreq:2=(\d+)\s+TLVChanWidth:1=(\d+)\s+TLVChanNum:1=(\d+)/g)].map(m => ({baseMHz: +m[1], width: +m[2], channel: +m[3]}));
const sub3 = results.find(r => r.subband === 0x13);
const sub3Mismatch = sub3.entries.map((e, i) => ({channel: e.channel, baseMHz: e.baseMHz, reference: referencePairs[i]})).filter(e => !e.reference || e.baseMHz !== e.reference.baseMHz || e.channel !== e.reference.channel);
const sub1 = results.find(r => r.subband === 0x11), sub2 = results.find(r => r.subband === 0x12);
const cal = Buffer.from(fs.readFileSync(path.join(dir, 'WlanCalData_ext.conf'), 'utf8').match(/\b[0-9a-f]{2}\b/gi).map(x => parseInt(x, 16)));
const summary = {
    groups: results.map(r => ({subband: r.subband, hostcmdSize: r.hostcmdSize, payloadBytes: r.payloadBytes, entries: r.entries.length, baseMHz: [...new Set(r.entries.map(e => e.baseMHz))], channels: r.entries.map(e => e.channel)})),
    sub2EqualsLastSub1TLV: sub2.entries[0].rawHex === sub1.entries.at(-1).rawHex,
    fiveGHzRateVectorsIdentical: results.filter(r => r.subband !== 0).flatMap(r => r.entries).every(e => JSON.stringify(e.rates) === JSON.stringify(sub1.entries[0].rates)),
    sub3Mismatch,
    calibration: {textBytes: fs.statSync(path.join(dir, 'WlanCalData_ext.conf')).size, decodedBytes: cal.length, firstSixBytes: cal.subarray(0, 6).toString('hex'), wordAtOffset4: cal.readUInt16LE(4), remainingAfterSix: cal.length - 6}
};
fs.writeFileSync(path.join(dir, 'POWER-DECODED.json'), JSON.stringify({summary, results}, null, 2));
console.log(JSON.stringify(summary, null, 2));
