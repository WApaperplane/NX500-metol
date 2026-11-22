const fs = require('fs');
const path = require('path');
const assert = require('assert');
const dir = __dirname;
const decoded = JSON.parse(fs.readFileSync(path.join(dir, 'POWER-DECODED.json')));
const expectedChannels = [36,40,44,48,52,56,60,64,100,104,108,112,116,120,124,128,132,136,140,149,153,157,161,165,183,184,185,187,188,189,192,196,7,8,11,12,16,34,144];
const firstRecord = Buffer.from('240f0f0f0d0f0f0d0d0d0b0b0a0b0b0b08', 'hex');
const outputs = [];
for (const name of ['sd8887_uapsta.bin', 'sdio8887_sdio_combo.bin']) {
    const b = fs.readFileSync(path.join(dir, name));
    const blocks = [];
    let offset = 0;
    while (offset + 16 <= b.length) {
        const command = b.readUInt32LE(offset), address = b.readUInt32LE(offset + 4), length = b.readUInt32LE(offset + 8);
        assert(length <= b.length - offset - 16);
        blocks.push({offset, command, address, length, dataOffset: offset + 16});
        offset += 16 + length;
    }
    assert.equal(offset, b.length);
    assert.equal(blocks.at(-1).command, 4);
    const fileStart = b.indexOf(firstRecord);
    assert(fileStart >= 0);
    const owner = blocks.find(x => x.command === 1 && fileStart >= x.dataOffset && fileStart < x.dataOffset + x.length);
    assert(owner);
    const loadStart = owner.address + fileStart - owner.dataOffset;
    const table = Buffer.alloc(17 * expectedChannels.length);
    const covered = new Uint8Array(table.length);
    const contributions = [];
    // Map by each block's recorded destination; later blocks replace overlapping bytes.
    for (const block of blocks.filter(x => x.command === 1)) {
        const lo = Math.max(loadStart, block.address), hi = Math.min(loadStart + table.length, block.address + block.length);
        if (lo >= hi) continue;
        b.copy(table, lo - loadStart, block.dataOffset + lo - block.address, block.dataOffset + hi - block.address);
        covered.fill(1, lo - loadStart, hi - loadStart);
        contributions.push({headerFileOffset: block.offset, destination: '0x' + block.address.toString(16), length: block.length});
    }
    assert(covered.every(x => x === 1));
    const records = expectedChannels.map((channel, index) => {
        const record = table.subarray(index * 17, index * 17 + 17);
        assert.equal(record[0], channel);
        return {index, channel: record[0], powers: [...record.subarray(1)], loadAddress: '0x' + (loadStart + index * 17).toString(16)};
    });
    const responseChecks = decoded.results.filter(r => r.subband !== 0).map(group => ({
        subband: group.subband,
        matchesByRateIndex: group.entries.every(entry => {
            const record = records.find(r => r.channel === entry.channel);
            return record && entry.rates.every(([rate, power]) => record.powers[rate] === power);
        })
    }));
    assert(responseChecks.every(x => x.matchesByRateIndex));
    fs.writeFileSync(path.join(dir, name + '.compact-5g.bin'), table);
    outputs.push({file: name, fileStart: '0x' + fileStart.toString(16), loadStart: '0x' + loadStart.toString(16), recordBytes: 17, count: records.length, contributions, responseChecks, records});
}
const identical = fs.readFileSync(path.join(dir, outputs[0].file + '.compact-5g.bin')).equals(fs.readFileSync(path.join(dir, outputs[1].file + '.compact-5g.bin')));
assert(identical);
fs.writeFileSync(path.join(dir, 'COMPACT-TABLE-MAPPING.json'), JSON.stringify({identical, method: '16-byte block headers, map payload by recorded destination address in file order, later overlapping writes win', outputs}, null, 2));
console.log(JSON.stringify({identical, tables: outputs.map(x => ({file:x.file, fileStart:x.fileStart, loadStart:x.loadStart, count:x.count, channel183:x.records.find(r=>r.channel===183), blocks:x.contributions,responseChecks:x.responseChecks}))}, null, 2));
