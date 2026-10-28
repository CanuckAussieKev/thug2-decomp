// NOT ORIGINAL - JS compatibility operations and bounded generator task runner.
// CStruct::AddComponent and docs/script-runtime.md sections 3, 5 and 6.
globalThis.qb = (() => {
    const scripts = new Map(), tasks = new Map();
    let nextId = 1, active = null, lastFrame = -1;
    const scalar = v => {
        if (!v || ![1, 2, 13].includes(v.type) || !Number.isInteger(v.bits) || v.bits < 0 || v.bits > 0xffffffff)
            throw Error('Invalid QB scalar');
        return {type:v.type, bits:v.bits};
    };
    const uint = n => { if (!Number.isInteger(n) || n < 0 || n > 0xffffffff) throw Error('Invalid QB uint32'); return n; };
    function merge(a, b) {
        const out = [];
        for (const [name, value] of [...a, ...b]) {
            uint(name); const v = scalar(value);
            for (let i = out.length - 1; i >= 0; --i) {
                const [n, old] = out[i];
                const same = old.type === v.type && (n !== 0 || v.type !== 13 || old.bits === v.bits);
                const numeric = [1, 2].includes(old.type) && [1, 2].includes(v.type);
                if (n === name && (same || numeric)) out.splice(i, 1);
            }
            if (out.length >= 4096) throw Error('QB parameter count limit');
            out.push([name, v]);
        }
        return out;
    }
    function assign(p, n, v) {
        if (v !== undefined) { const out = merge(p, [[n, v]]); p.splice(0, p.length, ...out); }
    }
    function fields(p, items) {
        let out = [];
        for (const item of items) {
            if (item.spread) out = merge(out, p);
            else if (item[1] !== undefined) out = merge(out, [item]);
        }
        return out;
    }
    function step(site) {
        if (!active) throw Error('No active QB task');
        active.site = site;
        if (--active.fuel < 0) throw Error('QB statement fuel exhausted');
    }
    function* call(name, args, p, site) {
        const t = active;
        if (++t.depth > 32) throw Error('QB call depth limit');
        try {
            step(site);
            if (scripts.has(name)) {
                const returned = yield* scripts.get(name)(args, false);
                const out = merge(p, returned); p.splice(0, p.length, ...out);
                return true; // A QB-call condition counts as true, irrespective of returned values.
            }
            const r = __qbHost(name, args, p);
            if (!r || typeof r.condition !== 'boolean') throw Error('Invalid QB host response');
            const out = merge(p, r.outputs); p.splice(0, p.length, ...out);
            if (r.frames !== undefined) yield {frames:uint(r.frames)};
            return r.condition;
        } finally { --t.depth; }
    }
    function snapshot(t) { return {status:t.status, parameters:merge([], t.parameters), message:t.message}; }
    return Object.freeze({
        merge, assign, fields, step, call,
        get(p, name) { for (let i = p.length - 1; i >= 0; --i) if (p[i][0] === name) return p[i][1]; },
        truth(v) {
            if (v === undefined) throw Error('Absent QB expression parameter');
            scalar(v);
            // ExecuteCommand dispatches a Name expression as a command; it
            // does not treat a nonzero checksum as an ordinary true value.
            if (v.type === 13) throw Error('QB checksum expression dispatch is not implemented');
            return v.bits !== 0;
        },
        frame(p, root) { if (root) active.parameters = p; },
        module(m) { if (m.format !== 1) throw Error('Unsupported QB module format'); },
        define(n, fn) { uint(n); if (tasks.size) throw Error('Cannot load QB scripts with retained tasks'); if (scripts.has(n)) throw Error('Duplicate QB script'); scripts.set(n, fn); },
        start(n, args, fuel) {
            if (tasks.size >= 256 || nextId > 0xffffffff) throw Error('QB task capacity limit');
            if (!scripts.has(n)) throw Error('Unknown root QB script');
            if (!Number.isInteger(fuel) || fuel < 0 || fuel > 1000000) throw Error('Invalid QB fuel');
            const t = {generator:scripts.get(n)(merge([], args), true), parameters:[], fuel, depth:0, site:null, remaining:0, waiting:false, frame:-1, status:'ready', message:''};
            const id = nextId++; tasks.set(id, t); return id;
        },
        update(id, frame) {
            uint(frame); const t = tasks.get(id); if (!t) throw Error('Unknown QB task');
            if (active) throw Error('Reentrant QB execution');
            if (frame < lastFrame) throw Error('QB frame went backwards'); lastFrame = frame;
            if (t.frame === frame || !['ready', 'waiting'].includes(t.status)) return snapshot(t);
            t.frame = frame; active = t;
            try {
                if (t.waiting && t.remaining) { --t.remaining; return snapshot(t); }
                t.waiting = false;
                for (;;) {
                    const r = t.generator.next();
                    if (r.done) { t.parameters = merge([], r.value); t.status = 'finished'; break; }
                    const count = uint(r.value.frames);
                    if (count === 0) continue; // Wait 0 resumes in the same Update.
                    t.remaining = count - 1; t.waiting = true; t.status = 'waiting'; break;
                }
            } catch (e) {
                t.status = 'fault';
                t.message = String(e) + (t.site ? ` at ${t.site.source} script 0x${t.site.script.toString(16)} QB offset 0x${t.site.offset.toString(16)}` : '');
            } finally { active = null; }
            return snapshot(t);
        },
        cancel(id) { return tasks.delete(id); },
    });
})();
