// NOT ORIGINAL - JS compatibility operations and bounded generator task runner.
// CStruct::AddComponent and docs/script-runtime.md sections 3, 5 and 6.
//
// In the game (src/script_ts, docs/qb-ts.md "In the game") the C++ interpreter drives: globalThis.__qbGame is its
// natives, a script level is begun / resumed by it (begin, resume), every command call yields a request
// {call, args, on} it carries out on the script's record (its result is the resume's value), symbols are the game's.
globalThis.qb = (() => {
    const game = globalThis.__qbGame;
    const scripts = new Map(), symbols = new Map(), tasks = new Map();
    let nextId = 1, active = null, lastFrame = -1, loadingFormat = 1, loadFuel = 1000000, inlineCount = 0, loadingOwner = null;
    // A name read with arguments that turned out not to be a C function: its arguments, for the dispatch of that name
    // (name / dispatch below).
    let leftover = null;
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
        // (in the game nothing is counted: QB has no such bound - a level's whole node array may be copied - and the
        // bridge's interrupt budget stops a runaway script)
        if (game) return;
        uint(cost);
        if (cost > task.fuel) throw Error('QB value/statement fuel exhausted');
        task.fuel -= cost;
    };
    const context = site => {
        const task = active;
        if (task) task.site = site;
        return {site, owner:task?.owner ?? task?.id ?? 0, symbol:n => game ? game.symbol(n) : symbols.get(n), charge:cost => {
            if (task || game) consume(task, cost);
            else { uint(cost); if (cost > loadFuel) throw Error('QB module value fuel exhausted'); loadFuel -= cost; }
        }};
    };
    // Boundary copies have their own bounded work budget, so observing a task
    // does not consume its execution fuel or depend on module-loading state.
    const boundary = site => {
        const budget = {fuel:250000};
        return {site:site ?? {source:'<boundary>',script:0,offset:0}, owner:0, symbol:n => game ? game.symbol(n) : symbols.get(n), charge:n => consume(budget, n)};
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
        leftover = null; // (a name's leftover arguments are its statement's only)
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
            // In the game: the interpreter runs it (execute_command's dispatch, on the script's record).
            if (game && !recording) return yield {call: name, args, on};
            if (game) {
                let unnamed = 0;
                for (const [n, value] of args) noteType(`arg ${hex(name)} ${n ? hex(n) : '#' + unnamed++}`, typeOf(value));
                const before = game.frame();
                const result = yield {call: name, args, on};
                noteType(`wait ${hex(name)}`, game.frame() === before ? 'same frame' : 'waited');
                return result;
            }
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
    // --- [debug] ts_types=1 in the game: the types each site of the TypeScript sees, counted (tools/qbjs/types.ts
    // turns them, with what the .qb files say, into typings - the facts the natural-TypeScript steps rest on) ---
    const recording = !!(game && game.recording);
    const seen = new Map(); // site -> Map(type -> count)
    const TYPE_NAMES = {0:'empty', 1:'int', 2:'float', 3:'string', 4:'localstring', 5:'pair', 6:'vector', 7:'script', 10:'struct', 12:'array', 13:'name', 20:'struct', 21:'array'};
    function typeOf(v) {
        if (v === undefined || v === null) return 'none';
        if (v.kind === 'callable') return 'callable';
        if (v.type === 12 || v.type === 21) {
            const a = v.type === 21 ? v.target : v;
            return a.elements?.length ? `array<${TYPE_NAMES[a.elementType] ?? a.elementType}>` : 'array<empty>';
        }
        return TYPE_NAMES[v.type] ?? `type${v.type}`;
    }
    function noteType(site, type) {
        let counts = seen.get(site);
        if (!counts) seen.set(site, counts = new Map());
        counts.set(type, (counts.get(type) ?? 0) + 1);
    }
    const hex = n => (n >>> 0).toString(16).padStart(8, '0');
    // A number for a definition that no symbol has (the game's TypeScript bodies and script values name it).
    function freshId() {
        let n;
        do n = (0xc0de0000 + inlineCount++) >>> 0;
        while (scripts.has(n) || symbols.has(n) || (game && game.symbol(n) !== undefined));
        return n;
    }
    function copyParameters(t, p) {
        return t.format === 2 ? values().merge([], p, boundary(t.site)) : merge([], p);
    }
    function snapshot(t) { return {status:t.status, parameters:copyParameters(t, t.parameters), message:t.message}; }
    return Object.freeze({
        runtimeVersion: 2,
        recording,
        // note(site, value): a value seen there (only while recording); noteType: a type already named.
        note(site, value) { if (recording) noteType(site, typeOf(value)); },
        noteType(site, type) { if (recording) noteType(site, type); },
        typeOf,
        // The counts as JSON ({version, sites: {site: {type: count}}}), what loadTypes was given added in.
        recordedTypes() {
            const sites = {};
            for (const [site, counts] of seen) sites[site] = Object.fromEntries(counts);
            return JSON.stringify({version: 1, sites});
        },
        // A previous session's counts (the file recordedTypes was saved to), added to this one's.
        loadTypes(json) {
            const sites = JSON.parse(json)?.sites ?? {};
            for (const site of Object.keys(sites))
                for (const [type, count] of Object.entries(sites[site])) {
                    let counts = seen.get(site);
                    if (!counts) seen.set(site, counts = new Map());
                    counts.set(type, (counts.get(type) ?? 0) + Number(count));
                }
        },
        // `(expression) args` as a statement or an if's condition (execute_command): a name is called (0: nothing,
        // false), anything else is only its truth - arguments after it are outside what this runtime does.
        // (expression):Command - the object's name: the expression must give a name.
        objectName(value) {
            if (value?.type !== 13) throw Error('QB object-qualified call on an expression that is not a name');
            return value.bits;
        },
        *dispatch(value, args, p, site) {
            const extra = leftover?.name === value?.bits && value?.type === 13 ? leftover.args : [];
            leftover = null;
            if (extra.length) {
                // The statement has read on to the expression's ')', which then stays the next token: the statement
                // loop runs it as a statement naming nothing, never moving past it - the original loops there for ever
                // once the command (and whatever it called) is done. Here the level ends as a runaway does.
                if (value.bits !== 0) yield* call(value.bits, [...extra, ...args], p, site);
                throw Error('QB statement fuel exhausted: a name with arguments in an expression - the original loops for ever at its closing parenthesis');
            }
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
            const kind = game ? (active ? game.symbolKind(n) : undefined)
                : !symbols.has(n) && active && typeof globalThis.__qbSymbolKind === 'function'
                ? globalThis.__qbSymbolKind(n) : undefined;
            if (kind === 'function') {
                if (game) return {type:1, bits:game.callFunction(n, args) ? 1 : 0};
                const r = __qbHost(n, args, [], active.object, false);
                return {type:1, bits:r?.condition ? 1 : 0};
            }
            if (kind === 'method') throw Error('QB member function in an expression is not implemented');
            const value = values().global(n, ctx);
            if (args.length) {
                // Not a C function: the evaluator stops the expression at the first argument (a value where an
                // operator was due) and the statement reads the rest as its own arguments - so when the expression's
                // name is run as a command (execute_command), they are its arguments (dispatch takes them, below).
                if (value?.type !== 13) throw Error('QB arguments after a name that is not a C function in an expression');
                leftover = {name: value.bits, args};
            }
            return value;
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
        frame(p, root) { if (root || game) active.parameters = p; },
        module(m) {
            if (tasks.size && !game) throw Error('Cannot load QB modules with retained tasks');
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
            if (tasks.size && !game) throw Error('Cannot load QB globals with retained tasks');
            if (loadingFormat !== 2) throw Error('QB globals require module format 2');
            const copy = values().copy(value, 'value', context(site));
            scripts.delete(n); symbols.set(n, copy);
            if (game) game.defineGlobal(n, copy);
        },
        define(n, fn) {
            uint(n);
            if (typeof fn !== 'function') throw Error('Invalid QB script function');
            if (tasks.size && !game) throw Error('Cannot load QB scripts with retained tasks');
            if (loadingFormat === 1 && scripts.has(n)) throw Error('Duplicate QB script');
            symbols.set(n, {kind:'callable',type:7,name:n});
            if (!game) {
                scripts.set(n, {fn,format:loadingFormat});
                return;
            }
            // In the game each definition has a number of its own, which its symbol's body names (defineScript): the
            // symbol table says which definition runs - a temporary load undone, a level unloaded - as for QB.
            const id = freshId();
            scripts.set(id, {fn, format: loadingFormat, owner: loadingOwner});
            game.defineScript(n, id);
        },
        // The module being loaded (the game: its file's) - what it defines belongs to it; forget(owner): all of that
        // let go of (its file unloaded).
        loadingAs(owner) { loadingOwner = owner; },
        forget(owner) {
            for (const [n, entry] of scripts) if (entry.owner === owner) scripts.delete(n);
        },
        // inline(fn): a script as a value (a structure's `script name ... endscript`; the C++ runs it - the CAS
        // tables' part scripts, ModelAssembler::launch_script). Its function is kept under a made-up name no symbol
        // has; the value is the body defineScript gives a script, naming it: 01, 4A <name>, 01, 24.
        inline(fn) {
            if (typeof fn !== 'function') throw Error('Invalid QB script function');
            const n = freshId();
            scripts.set(n, {fn, format: loadingFormat, owner: loadingOwner});
            return {type: 7, bytes: [1, 0x4a, n & 0xff, (n >>> 8) & 0xff, (n >>> 16) & 0xff, n >>> 24, 1, 0x24]};
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
        // --- the game's levels (a level: one call-stack level of a script record running a JavaScript script) ---
        // begin: a level of script `n` with the record's parameters `args`; owner: the record's id (stored randoms).
        begin(id, n, args, owner) {
            const entry = script(n);
            if (!entry) throw Error('Unknown QB script');
            if (tasks.has(id)) throw Error('QB level already begun');
            const t = {id, owner:uint(owner), format:entry.format, object:0, generator:null, parameters:[], fuel:0, fuelLimit:1000000,
                depth:0, site:null, remaining:0, waiting:false, frame:-1, status:'ready', message:'', dirty:true, assigned:[]};
            active = t;
            // (not as a root: its return values go to the interpreter, which adds them where the original does)
            try { t.generator = entry.fn(args, false); } finally { active = null; }
            tasks.set(id, t);
        },
        // resume: the level runs on - `params` is the record's parameter list now (the level's view is refreshed
        // from it), `result` the last request's result - until its next request ({call, args, on}) or its end
        // ({done, returned}); `params` in the answer when the level changed its parameters (else null).
        resume(id, params, result) {
            const t = tasks.get(id);
            if (!t) throw Error('Unknown QB level');
            if (active) throw Error('Reentrant QB execution');
            if (Array.isArray(params) && !t.dirty && !t.assigned.length) t.parameters.splice(0, t.parameters.length, ...params);
            t.fuel = t.fuelLimit; active = t;
            try {
                const r = t.generator.next(result);
                // Its changes: the whole list once (the defaults merged at its start), after that the assignments.
                const changed = t.dirty ? {params:t.parameters} : t.assigned.length ? {assigned:t.assigned} : {params:null};
                t.dirty = false;
                t.assigned = [];
                if (r.done) { tasks.delete(id); return {done:true, returned:r.value ?? [], ...changed}; }
                return {...r.value, ...changed};
            } catch (e) {
                tasks.delete(id);
                throw e;
            } finally { active = null; }
        },
        // A level gone (its record reset, its level popped): forgotten.
        drop(id) { tasks.delete(id); },
        // An assignment by the level itself (name, the value as stored): sent back with its next answer.
        touched(n, value) { if (game && active && !active.dirty) active.assigned.push([n, value]); },
    });
})();
