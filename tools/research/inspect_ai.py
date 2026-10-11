#!/usr/bin/env python3
"""Static AI bytecode inventory using the reviewed PyMS parameter schema."""
import argparse,ast,collections,hashlib,json,pathlib,struct
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--data',type=pathlib.Path,required=True)
p.add_argument('--schema',type=pathlib.Path,required=True)
args=p.parse_args()
schema_hash=hashlib.sha256(args.schema.read_bytes()).hexdigest()
if schema_hash != 'b30ea0c1c2c4350d4b8de6c910eaf647d6c526a8a2efb64ba966e5c659a5125c':
    p.error('Unknown schema hash; use pinned PyMS AIBIN.py')
tree=ast.parse(args.schema.read_text())
aiclass=next(n for n in tree.body if isinstance(n,ast.ClassDef) and n.name=='AIBIN')
labels=next(ast.literal_eval(n.value) for n in aiclass.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='short_labels' for t in n.targets))
init=next(n for n in aiclass.body if isinstance(n,ast.FunctionDef) and n.name=='__init__')
parnode=next(n.value for n in init.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Attribute) and t.attr=='parameters' for t in n.targets))
params=[[] if isinstance(n,ast.Constant) else [e.attr for e in n.elts] for n in parnode.elts]
assert len(params)==len(labels)
manifest=json.loads((args.data/'manifest.json').read_text())
tables={}; code={}; results={}
for filename,stride in [('aiscript.bin',16),('bwscript.bin',8)]:
    data=(args.data/'scripts'/filename).read_bytes(); table=struct.unpack_from('<I',data)[0]
    if not 4<=table<=len(data)-4: raise ValueError('Bad table pointer')
    entries=[]; cursor=table
    while data[cursor:cursor+4]!=b'\0'*4:
        if cursor+stride>len(data): raise ValueError('Truncated table')
        ident=data[cursor:cursor+4].decode('latin1'); loc=struct.unpack_from('<I',data,cursor+4)[0]
        row={'id':ident,'offset':loc}
        if stride==16: row.update(zip(('string_index','flags'),struct.unpack_from('<II',data,cursor+8)))
        entries.append(row); cursor+=stride
    starts=sorted(set(e['offset'] for e in entries if e['offset']))
    instructions={}; counts=collections.Counter(); padding=[]
    # Decode independent address intervals exactly as PyMS does. One last zero
    # before the next script entry is padding, not an incomplete goto.
    for begin,end in zip(starts,starts[1:]+[table]):
        at=begin
        while at<end:
            start=at; op=data[at]; at+=1
            if op==0 and at==end: padding.append(start); break
            if op>=len(params): raise ValueError('Unknown opcode %d at %d'%(op,start))
            values=[]; targets=[]
            for typ in params[op]:
                if typ=='ai_string':
                    null=data.find(b'\0',at,end)
                    if null<0: raise ValueError('Truncated inline string')
                    value=None; size=null-at+1
                else:
                    size=1 if typ in ('ai_byte','ai_compare') else (4 if typ=='ai_dword' else 2)
                    if at+size>end: raise ValueError('Truncated parameter at %d'%start)
                    value=int.from_bytes(data[at:at+size],'little')
                values.append(value)
                if typ=='ai_address': targets.append(value)
                at+=size
            instructions[start]={'opcode':op,'label':labels[op],'next':at,'targets':targets,'values':values}
            counts[labels[op]]+=1
    bad=[{'from':at,'to':dest} for at,n in instructions.items() for dest in n['targets'] if dest not in instructions]
    if bad: raise ValueError('Invalid jump boundaries '+str(bad[:10]))
    tables[filename]={e['id']:e for e in entries}; code[filename]=instructions
    results[filename]={'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),'table_offset':table,'table_entries':len(entries),'entries_with_code':sum(bool(e['offset']) for e in entries),'redirect_entries':sum(not e['offset'] for e in entries),'opcode_types':len(counts),'instruction_count':sum(counts.values()),'padding_bytes':len(padding),'opcode_counts':dict(sorted(counts.items())),'entries':entries}
def resolve(ident):
    entry=tables['aiscript.bin'].get(ident)
    if not entry: raise ValueError('AI ID missing '+ident)
    if entry['offset']: return 'aiscript.bin',entry['offset']
    entry=tables['bwscript.bin'].get(ident)
    if not entry: raise ValueError('BW redirect missing '+ident)
    return 'bwscript.bin',entry['offset']
def reach(ident):
    filename,start=resolve(ident); visited=set(); todo=[start]; counts=collections.Counter()
    while todo:
        at=todo.pop()
        if at in visited: continue
        node=code[filename].get(at)
        if node is None: raise ValueError('Unresolved instruction %s:%d'%(filename,at))
        visited.add(at); counts[node['label']]+=1
        todo.extend(node['targets'])
        # These stop local flow. Calls/conditionals/spawns conservatively keep
        # both paths. This is a static upper bound, never a runtime trace.
        if node['label'] not in ('goto','stop','return','kill_thread','fatal_error','debug'):
            if node['next'] in code[filename]: todo.append(node['next'])
    return filename,visited,counts
catalog={r['path']+'\\staredit\\scenario.chk' for r in manifest['map_catalog'] if r['path'] and r['path']!='null'}
scopes={}
for scope,maps in [('catalog', [r for r in manifest['campaign_found'] if r['path'] in catalog]),('all_found',manifest['campaign_found'])]:
    roots=collections.Counter(c['id'] for r in maps for c in r['ai_calls'])
    visited=collections.defaultdict(set); byroot={}
    for ident,n in sorted(roots.items()):
        fn,seen,counts=reach(ident); visited[fn].update(seen)
        byroot[ident]={'launch_occurrences_raw':n,'filename':fn,'reachable_instruction_count_upper_bound':len(seen),'opcode_types':sorted(counts)}
    totals=collections.Counter()
    for fn,seen in visited.items():
        totals.update(code[fn][at]['label'] for at in seen)
    conditions=collections.Counter(); actions=collections.Counter(); groups=collections.Counter()
    for row in maps:
        conditions.update({int(k):v for k,v in row['condition_types'].items()}); actions.update({int(k):v for k,v in row['action_types'].items()}); groups[row['path'].split('\\')[1]]+=1
    scopes[scope]={'maps':len(maps),'groups':dict(groups),'TRIG_records':sum(r.get('TRIG_count',0) for r in maps),'MBRF_records':sum(r.get('MBRF_count',0) for r in maps),'condition_types':dict(sorted(conditions.items())),'action_types':dict(sorted(actions.items())),'ai_launches_raw':sum(roots.values()),'ai_root_ids':len(roots),'roots':byroot,'reachable_instructions_upper_bound':sum(totals.values()),'reachable_opcode_types_upper_bound':len(totals),'reachable_opcode_counts_upper_bound':dict(sorted(totals.items()))}
result={'scope':'Static raw/upper-bound inventory; disabled actions and impossible trigger conditions included; no runtime proof','schema_sha256':hashlib.sha256(args.schema.read_bytes()).hexdigest(),'schema_source':'PyMS AIBIN.py bfc5d3aad0b5614a5aff72c223f8efa00afddfa4','parameter_schema_opcodes':len(labels),'files':results,'campaign_scopes':scopes,'extra_maps_not_in_catalog':[r['path'] for r in manifest['campaign_found'] if r['path'] not in catalog]}
(args.data/'ai-inventory.json').write_text(json.dumps(result,indent=2,ensure_ascii=False)+'\n')
print(json.dumps({'files':{fn:{k:r[k] for k in ('table_entries','entries_with_code','redirect_entries','opcode_types','instruction_count')} for fn,r in results.items()},'campaign_scopes':{name:{k:r[k] for k in ('maps','TRIG_records','MBRF_records','ai_launches_raw','ai_root_ids','reachable_opcode_types_upper_bound')} for name,r in scopes.items()},'extra_maps':result['extra_maps_not_in_catalog']},ensure_ascii=False))
