// NOT ORIGINAL - JS compatibility operations and bounded generator task runner.
// CStruct::AddComponent and docs/script-runtime.md sections 3, 5 and 6.
globalThis.qb = (() => {
    const scripts = new Map(), symbols = new Map(), tasks = new Map();
    let nextId = 1, active = null, lastFrame = -1, loadingFormat = 1, loadFuel = 1000000;
    let rng = null;
    const random = () => {
        if (globalThis.qbRandom?.apiVersion !== 1) throw Error('QB random API 1 required');
        if (!rng) rng = globalThis.qbRandom.create(0);
        return rng;
    };
    const scalar = v => {
        if (!v || ![1, 2, 13].includes(v.type) || !Number.isInteger(v.bits) || v.bits < 0 || v.bits > 0xffffffff)
            throw Error('Invalid QB scalar');
        return {type:v.type, bits:v.bits};
    };
    const uint = n => { if (!Number.isInteger(n) || n < 0 || n > 0xffffffff) throw Error('Invalid QB uint32'); return n; };
    const values = () => {
        if (globalThis.qbValues?.apiVersion !== 2) throw Error('QB values API 2 required');
        return globalThis.qbValues;
    };
    const consume = (task, cost) => {
        uint(cost);
        if (cost > task.fuel) throw Error('QB value/statement fuel exhausted');
        task.fuel -= cost;
    };
    const context = site => {
        const task = active;
        if (task) task.site = site;
        return {site, owner:task?.id ?? 0, symbol:n => symbols.get(n), charge:cost => {
            if (task) consume(task, cost);
            else { uint(cost); if (cost > loadFuel) throw Error('QB module value fuel exhausted'); loadFuel -= cost; }
        }};
    };
    // Boundary copies have their own bounded work budget, so observing a task
    // does not consume its execution fuel or depend on module-loading state.
    const boundary = site => {
        const budget = {fuel:250000};
        return {site:site ?? {source:'<boundary>',script:0,offset:0}, owner:0, symbol:n => symbols.get(n), charge:n => consume(budget, n)};
    };
    const script = (n, ctx) => {
        const direct = scripts.get(n);
        if (direct) return direct;
        if (symbols.has(n) && globalThis.qbValues?.apiVersion === 2) {
            const entry = values().lookupGlobal(n, ctx ?? boundary({source:'<dispatch>',script:n,offset:0}));
            if (entry?.kind === 'callable' && entry.type === 7) return scripts.get(entry.name);
        }
    };
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
    // on: `object:Command` - the object's name (execute_command's colon form). The object must exist (where it
    // doesn't the original hangs: it re-runs the ':' for ever); a script then runs with it as its object, any
    // other command goes to the host as qualified (only member functions run on an object; C functions are false).
    // Without on, a command runs on the task's current object (a script's callees inherit it).
    function* call(name, args, p, site, on) {
        const t = active;
        if (++t.depth > 32) throw Error('QB call depth limit');
        const caller = t.object;
        try {
            step(site);
            if (on !== undefined) {
                uint(on);
                const exists = typeof globalThis.__qbFindObject === 'function' ? globalThis.__qbFindObject(on) : true;
                if (!exists) throw Error('QB object-qualified call on a missing object (the original hangs here)');
            }
            if (name === 0) return false; // a command named by a parameter that holds no name: nothing runs
            const callee = script(name, t.format === 2 ? context(site) : undefined);
            if (callee) {
                if (callee.format !== t.format) throw Error('Mixed QB module formats in one call stack are unsupported');
                if (on !== undefined) t.object = on;
                const returned = yield* callee.fn(args, false);
                if (t.format === 2) values().append(p, returned, context(site));
                else { const out = merge(p, returned); p.splice(0, p.length, ...out); }
                return true; // A QB-call condition counts as true, irrespective of returned values.
            }
            if (symbols.has(name)) return false; // A data symbol is not a command.
            const r = on === undefined ? __qbHost(name, args, p, t.object, false) : __qbHost(name, args, p, on, true);
            if (r === undefined) return false; // Missing command: game's false condition.
            if (!r || typeof r.condition !== 'boolean') throw Error('Invalid QB host response');
            if (t.format === 2) values().append(p, r.outputs, context(site));
            else { const out = merge(p, r.outputs); p.splice(0, p.length, ...out); }
            if (r.frames !== undefined) yield {frames:uint(r.frames)};
            return r.condition;
        } finally { --t.depth; t.object = caller; }
    }
    function copyParameters(t, p) {
        return t.format === 2 ? values().merge([], p, boundary(t.site)) : merge([], p);
    }
    function snapshot(t) { return {status:t.status, parameters:copyParameters(t, t.parameters), message:t.message}; }
    return Object.freeze({
        runtimeVersion: 2,
        // `(expression) args` as a statement or an if's condition (execute_command): a name is called (0: nothing,
        // false), anything else is only its truth - arguments after it are outside what this runtime does.
        *dispatch(value, args, p, site) {
            if (value?.type === 13) return value.bits === 0 ? false : yield* call(value.bits, args, p, site);
            if (args.length) throw Error('QB arguments after an expression that is not a name');
            return values().statementTruth(value, context(site));
        },
        // A name in an expression (push_name): a global's value - a script's name stays a name -, or, when the host
        // says it is a C function, that function called with args (parse_arguments to the ')'): its result as an
        // Integer 0 / 1, its outputs dropped. Arguments after anything else, or a member function there, are
        // outside what this runtime does (the original reads the arguments on as more of the expression).
        name(n, args, site) {
            uint(n);
            const ctx = context(site);
            const kind = !symbols.has(n) && active && typeof globalThis.__qbSymbolKind === 'function'
                ? globalThis.__qbSymbolKind(n) : undefined;
            if (kind === 'function') {
                const r = __qbHost(n, args, [], active.object, false);
                return {type:1, bits:r?.condition ? 1 : 0};
            }
            if (kind === 'method') throw Error('QB member function in an expression is not implemented');
            if (args.length) throw Error('QB arguments after a name that is not a C function in an expression');
            return values().global(n, ctx);
        },
        merge, assign, fields, step, call, context, random,
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
        module(m) {
            if (tasks.size) throw Error('Cannot load QB modules with retained tasks');
            if (m?.format !== 1 && m?.format !== 2) throw Error('Unsupported QB module format');
            if (m.format === 2) {
                values();
                if (m.requires?.values !== 2 || Object.keys(m.requires).some(k => k !== 'values' && k !== 'random') ||
                    (m.requires.random !== undefined && m.requires.random !== 1))
                    throw Error('Unsupported QB module requirements');
                if (m.requires.random === 1) random();
            }
            loadingFormat = m.format; loadFuel = 1000000;
        },
        defineGlobal(n, value, site) {
            uint(n);
            if (tasks.size) throw Error('Cannot load QB globals with retained tasks');
            if (loadingFormat !== 2) throw Error('QB globals require module format 2');
            const copy = values().copy(value, 'value', context(site));
            scripts.delete(n); symbols.set(n, copy);
        },
        define(n, fn) {
            uint(n);
            if (typeof fn !== 'function') throw Error('Invalid QB script function');
            if (tasks.size) throw Error('Cannot load QB scripts with retained tasks');
            if (loadingFormat === 1 && scripts.has(n)) throw Error('Duplicate QB script');
            scripts.set(n, {fn,format:loadingFormat});
            symbols.set(n, {kind:'callable',type:7,name:n});
        },
        start(n, args, fuel, object = 0) {
            if (tasks.size >= 256 || nextId > 0xffffffff) throw Error('QB task capacity limit');
            const entry = script(n);
            if (!entry) throw Error('Unknown root QB script');
            if (!Number.isInteger(fuel) || fuel < 0 || fuel > 1000000) throw Error('Invalid QB fuel');
            const t = {id:nextId, format:entry.format, object:uint(object), generator:null, parameters:[], fuel, fuelLimit:fuel, depth:0, site:null, remaining:0, waiting:false, frame:-1, status:'ready', message:''};
            t.generator = entry.fn(copyParameters(t, args), true);
            const id = nextId++; tasks.set(id, t); return id;
        },
        startScalar(n,args,fuel) {
            const entry=script(n);
            if (entry?.format===2) throw Error('Format-2 native tasks require startRich; scalar boundary rejected before execution');
            return this.start(n,args,fuel);
        },
        abort(id) {
            const t=tasks.get(id);if(!t)throw Error('Unknown QB task');
            active=null;t.generator=null;t.status='fault';
            t.message='QB execution interrupt budget exhausted' + (t.site ?
                ` at ${t.site.source} script 0x${t.site.script.toString(16)} QB offset 0x${t.site.offset.toString(16)}` : '');
            if(rng)rng.forget(id);
            return snapshot(t);
        },
        update(id, frame) {
            uint(frame); const t = tasks.get(id); if (!t) throw Error('Unknown QB task');
            if (active) throw Error('Reentrant QB execution');
            if (frame < lastFrame) throw Error('QB frame went backwards'); lastFrame = frame;
            if (t.frame === frame || !['ready', 'waiting'].includes(t.status)) return snapshot(t);
            t.frame = frame; t.fuel = t.fuelLimit; active = t;
            try {
                if (t.waiting && t.remaining) { --t.remaining; return snapshot(t); }
                t.waiting = false;
                for (;;) {
                    const r = t.generator.next();
                    if (r.done) { t.parameters = copyParameters(t, r.value); t.status = 'finished'; break; }
                    const count = uint(r.value.frames);
                    if (count === 0) continue; // Wait 0 resumes in the same Update.
                    t.remaining = count - 1; t.waiting = true; t.status = 'waiting'; break;
                }
            } catch (e) {
                if(e?.name==='InternalError' && /out of memory|stack overflow/.test(e.message)) throw e;
                t.status = 'fault';
                t.message = String(e) + (t.site ? ` at ${t.site.source} script 0x${t.site.script.toString(16)} QB offset 0x${t.site.offset.toString(16)}` : '');
            } finally { active = null; }
            if (rng && (t.status === 'finished' || t.status === 'fault')) rng.forget(t.id);
            return snapshot(t);
        },
        cancel(id) { const removed=tasks.delete(id); if (removed && rng) rng.forget(id); return removed; },
    });
})();
