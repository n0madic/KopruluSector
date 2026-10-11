#!/usr/bin/env python3
"""Recover Nuke, Recall, Dark Swarm and additional native reaction decisions.

Default mode extracts exact-hash static witnesses. Explicit execution is private,
bounded x86 emulation with reviewed original callees and no helper stubs.
"""
import argparse, hashlib, json, platform, struct
from pathlib import Path
import capstone, pefile
import inspect_native_combat_ai_117 as combat
import inspect_native_dat_requirements_117 as requirements
import inspect_native_primary_orders_117 as primary
import probe_native_placement_117 as spatial
import inspect_native_reaction_spells_117 as reaction

TARGET_HASH = combat.TARGET_HASH
WEAPONS_HASH = '80de165015519f9bed46ea8858195c2d2d0120b65b9e55e1c0a50b19135f4b62'
TECH_HASH = '98f9805ebc8efc707492b7406871298d39d6e94931458aca1f82feaf18a271af'
WITNESSES = {
    'ready_linked_nuclear_silo': (0x436F60,0x436FB4),
    'nuclear_rate_gate': (0x4832A0,0x4832C7),
    'record_nuclear_tick': (0x483270,0x483282),
    'valid_detector': (0x431A90,0x431ADF),
    'recall_region_gate': (0x4AEA70,0x4AEA9B),
    'unit_region_from_sprite': (0x481BA0,0x481BB0),
    'region_from_pixel': (0x481B10,0x481B98),
    'ground_weapon': (0x41FC20,0x41FC3C),
    'effective_projectile_miss': (0x41FD10,0x41FD82),
    'terrain_or_swarm_miss': (0x41FA00,0x41FA9E),
    'tile_flags': (0x412C10,0x412C2D),
    'completed_count': (0x4143E0,0x4143F4),
    'terrain_elevation': (0x445690,0x44571D),
    'player_unit_list_head': (0x4256F0,0x4256F8),
    'unit_bounds': (0x409CC0,0x409D18),
    'any_overlapping_type': (0x432070,0x43208B),
    'type_equals': (0x432410,0x43241A),
    'first_overlapping_predicate': (0x443110,0x44317F),
    'overlapping_unit_list': (0x4224C0,0x422756),
    'release_spatial_list': (0x4222B0,0x4222C8),
    'append_position_order': (0x429EE0,0x429F0F),
    'irradiate_organic_nonbuilding': (0x438B50,0x438B7E),
    'additional_restoration_caster': (0x482450,0x482466),
    'rectangle_distance_within_range': (0x4229A0,0x422ADD),
    'emp_enemy_shield_score': (0x433CD0,0x433D08),
    'original_rng': (0x478200,0x478232),
}

def dependencies():
    return {Path(m.__file__).name: hashlib.sha256(Path(m.__file__).read_bytes()).hexdigest()
            for m in (combat, requirements, primary, spatial, reaction)}

def dat_profile(orders, weapons, tech):
    primary.stock_orders(orders)
    assert hashlib.sha256(weapons).hexdigest() == WEAPONS_HASH
    assert hashlib.sha256(tech).hexdigest() == TECH_HASH
    return {'weapon_flags': list(struct.unpack_from('<130H',weapons,910)),
            'order_tech': list(orders[2835:3024]),
            'energy': list(struct.unpack_from('<44H',tech,264))}

def execute(pe, orders, dat):
    if platform.machine().lower() not in ('x86_64','amd64'): raise RuntimeError('Use validated x86_64 runtime')
    import unicorn
    from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_ESP,UC_X86_REG_EIP
    caster,target,caster2,friend = (0x5DA3A0+i*0x150 for i in range(4))
    sprites,images,pool,ai,stack,stop = 0x800000,0x801000,0x810000,0x812000,0x900000,0xA00000
    player = 7
    image=pe.get_memory_mapped_image();size=(pe.OPTIONAL_HEADER.SizeOfImage+4095)&~4095
    allow=list(WITNESSES.values())+list(reaction.WITNESSES.values())+list(requirements.WITNESSES.values())+list(primary.BLOCKS.values())+list(spatial.BLOCKS.values())
    allow += [combat.BLOCKS[k] for k in ('approximate_distance','cloak_detection_rejection','rounded_life','order_energy_cost','energy_insufficient',
              'spell_candidate_callbacks_a1','spell_candidate_callbacks_a2','spell_selectors_a','spell_selectors_b','spell_candidate_callbacks_b1',
              'spell_candidate_callbacks_b2','spell_candidate_callbacks_b3','spell_candidate_callbacks_c','spell_candidate_callbacks_d1','spell_candidate_callbacks_d2',
              'spell_area_enemy_life_score','spell_area_allied_life_score','maelstrom_area_enemy_life_score','radius_callback_iteration',
              'nearest_rectangle_search_setup','nearest_rectangle_search_loop','campaign_ai_flag','global_spell_cooldown_zero',
              'reaction_spell_dispatch','spell_ready','reserve_spell_target','nuke_area_score','is_subunit')]
    cases=[]
    def put(u,a,v,n=4):u.mem_write(a,(v&((1<<(8*n))-1)).to_bytes(n,'little'))
    def get(u,a,n=4):return int.from_bytes(u.mem_read(a,n),'little')
    def unit(u,a,ident,owner,x,y,hp=100,status=1,properties=0):
        j=(a-caster)//0x150;s=sprites+j*0x100;im=images+j*0x100
        u.mem_write(a,bytes(0x150));put(u,a+0x64,ident,2);put(u,a+0x4c,owner,1);put(u,a+0x4d,1,1)
        put(u,a+8,hp*256);put(u,a+0xdc,status);put(u,a+0xc,s)
        put(u,a+0x10,x,2);put(u,a+0x12,y,2);put(u,s+0x14,x,2);put(u,s+0x16,y,2);put(u,s+0xc,255,1)
        for f in(0x18,0x1c,0x20):put(u,s+f,im)
        put(u,0x521660+ident*4,properties);put(u,0x520708+ident*4,100*256);put(u,0x520458+ident,0,1)
        return s
    def spatial_indices(u,addresses):
        for axis,table in enumerate((0x5c99b8,0x5d03f8)):
            values=[]
            for a in addresses:
                s=get(u,a+0xc);v=get(u,s+0x14+axis*2,2);idx=1+(a-caster)//0x150
                values.extend([(v-8,idx,False),(v+8,idx,True)])
            values.sort()
            for pos,(coord,idx,high)in enumerate(values):
                put(u,table+pos*8,idx);put(u,table+pos*8+4,coord)
                put(u,caster+(idx-1)*0x150+(0x144 if axis else 0x13c)+(4 if high else 0),pos)
        put(u,0x5c99b0,len(addresses)*2)
        for a in(0x5d6e48,0x5d6e5c,0x5da38c):put(u,a,0)
    def new(ident=9,target_id=0,target_owner=7,hp=50,properties=0):
        u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x400000,size);u.mem_write(0x400000,image)
        for a,n in((sprites,0x30000),(stack,0x10000),(stop,0x1000)):u.mem_map(a,n)
        u.mem_write(0x67d068,bytes(8*0x4e8));u.mem_write(0x534914,bytes([1])*144);put(u,0x534914+player*12,0,1)
        unit(u,caster,ident,player,512,512,properties=0x200000);unit(u,target,target_id,target_owner,640,512,hp,properties=properties)
        unit(u,caster2,46,player,512,1024,properties=0x200000);unit(u,friend,15,player,656,512,hp=40)
        put(u,caster+0x134,ai);put(u,caster2+0x134,ai+0x20)
        put(u,caster+0xa2,200*256,2);put(u,caster2+0xa2,200*256,2)
        put(u,0x5432fc,0);put(u,0x6be654,0);put(u,0x536720,1,1);put(u,0x508d50,0)
        for _,(_,_,off,capacity,_,_)in requirements.TABLES.items():u.mem_write(off,struct.pack('<'+'H'*capacity,*([65535]*capacity)))
        for a,count in((0x534104,24*12),(0x536330,20*12),(0x534224,24*12),(0x536420,20*12)):u.mem_write(a,bytes([1])*count)
        u.mem_write(0x52c0c4,struct.pack('<'+'I'*(228*12),*([1]*(228*12))))
        u.mem_write(0x529604,struct.pack('<'+'I'*(228*12),*([1]*(228*12))))
        u.mem_write(0x51b800,bytes(dat['order_tech']));u.mem_write(0x51f868,struct.pack('<44H',*dat['energy']))
        u.mem_write(0x5251a8,struct.pack('<130H',*dat['weapon_flags']))
        for row in orders:
            put(u,0x51b200+row['id'],row['interruptible'],1);put(u,0x51b980+row['id']*2,row['highlight'],2);put(u,0x51b080+row['id'],row['subunit_inheritance'],1)
        put(u,0x5aef00,pool);put(u,0x5aef04,pool+19*20);put(u,0x5aef08,0)
        for j in range(20):
            put(u,pool+j*20,pool+(j-1)*20 if j else 0);put(u,pool+j*20+4,pool+(j+1)*20 if j<19 else 0)
        put(u,0x6ab4f8,pool+0x1000);put(u,pool+0x1000,0,2)
        spatial_indices(u,[caster,target]);put(u,0x543228,0,2);put(u,0x54322a,0,2);put(u,0x54322c,2048,2);put(u,0x54322e,2048,2)
        return u
    def call(u,start,ecx=caster,edx=0,args=()):
        count,reached,trace,pending=0,False,[],[];entries={lo for lo,hi in allow}
        def hook(m,a,n,_):
            nonlocal count,reached
            while pending and a==pending[-1][0]:
                ret,j=pending.pop();trace[j]['returned_eax']=m.reg_read(UC_X86_REG_EAX)
            if a==stop:reached=True;m.emu_stop();return
            if not any(lo<=a and a+n<=hi for lo,hi in allow):raise RuntimeError(f'Unreviewed instruction {a:#x}; trace{trace[-8:]}')
            count+=1
            if a in entries:
                sp=m.reg_read(UC_X86_REG_ESP);trace.append({'callee':hex(a),'ecx':m.reg_read(UC_X86_REG_ECX),'edx':m.reg_read(UC_X86_REG_EDX),'stack_arguments':[get(m,sp+4+j*4)for j in range(6)]});pending.append((get(m,sp),len(trace)-1))
        h=u.hook_add(UC_HOOK_CODE,hook);sp=stack+0xe000;u.mem_write(sp,struct.pack('<'+'I'*(len(args)+1),stop,*(v&0xffffffff for v in args)))
        u.reg_write(UC_X86_REG_ESP,sp);u.reg_write(UC_X86_REG_ECX,ecx);u.reg_write(UC_X86_REG_EDX,edx)
        try:u.emu_start(start,stop+1,count=100000)
        except unicorn.UcError as e:raise RuntimeError(f'{e} at {u.reg_read(UC_X86_REG_EIP):#x}; trace{trace[-8:]}')from e
        finally:u.hook_del(h)
        if not reached:raise RuntimeError('No complete return within100000 instructions')
        return {'complete_return':True,'instruction_count':count,'return_eax':u.reg_read(UC_X86_REG_EAX),'native_call_trace':trace}
    def record(label,inputs,ex,actual,expected):
        assert actual==expected,(label,inputs,actual,expected)
        cases.append({'label':label,'fixture_inputs':inputs,'execution':ex,'observed':actual,'expected_from_static_contract':expected,'matched':True,'helper_stubs':[]})

    p=0x67d068+player*0x4e8
    # Real nuclear-silo readiness and attachment list.
    for ready,skip_busy,parent_kind in((0,1,'none'),(1,1,'none'),(1,1,'same ghost'),(1,1,'other unit'),(1,1,'silo'),(1,0,'other unit')):
        u=new(ident=1);unit(u,caster2,108,player,800,512,properties=1);unit(u,friend,14,player,800,512)
        put(u,caster2+0xd0,friend);put(u,caster2+0xd4,ready);put(u,0x665b08+player*4,caster2)
        parent={'none':0,'same ghost':caster,'other unit':target,'silo':caster2}[parent_kind];put(u,friend+0x80,parent)
        ex=call(u,0x436f60,args=(skip_busy,caster));ok=ready and(not skip_busy or parent_kind!='other unit')
        record('ready nuclear silo scan follows native attachment and busy-caster exceptions',{'ready_dword':ready,'skip_busy':skip_busy,'missile_parent':parent_kind,'owner_list_head':caster2},ex,ex['return_eax'],caster2 if ok else 0)
    for rate,last,now in((0,100,99),(0,100,100),(1,100,159),(1,100,160),(2,100,219),(2,100,220),(255,0xfffffff0,0),(255,0xfffffff0,15284)):
        u=new();put(u,p+0x212,rate,1);put(u,p+0x214,last);put(u,0x5349d8,now);ex=call(u,0x4832a0,ecx=player)
        record('nuclear rate gate uses inclusive unsigned current tick versus wrapped last-plus-rate-times-sixty',{'rate_byte':rate,'last_tick':last,'now_tick':now},ex,ex['return_eax'],int(now>=((last+rate*60)&0xffffffff)))
    # Nuke score: contribution order matters because important buildings SET800.
    for ident,properties,status,hp,shields,disabled,expected in((0,0,1,799,0,0,799),(0,0,1,800,0,0,800),(7,8,0x20001,50,20,0,70),
        (106,1,0x20001,1000,0,0,0),(103,1,0x20001,1,0,0,800),(146,1,0x20001,1,0,0,800),(124,0x8001,0x20001,1,0,0,800),
        (124,0x8001,0x20401,1,0,1,0),(0,0,0x4000001,1000,0,0,0)):
        u=new(target_id=ident,target_owner=0,hp=hp,properties=properties);put(u,target+0xdc,status);put(u,target+0x60,shields*256);put(u,0x520458+ident,1 if shields else 0,1)
        put(u,0x665d28,0);ex=call(u,0x432150,ecx=target,edx=caster)
        record('native Nuke area score distinguishes worker building status life and hard-eight-hundred types',{'id':ident,'properties':properties,'status':status,'hp':hp,'shields':shields,'disabled':disabled},ex,get(u,0x665d28),expected)
    for initial,properties in((1000,1),(2000,0x8001),(17,1),(0,1)):
        u=new(target_id=103,target_owner=0,hp=1,properties=properties);put(u,target+0xdc,0x20001);put(u,0x665d28,initial);ex=call(u,0x432150,ecx=target,edx=caster)
        record('important Nuke building assigns eight hundred rather than adding or taking maximum',{'id':103,'properties':properties,'initial_score':initial},ex,get(u,0x665d28),800)
    def nuclear_fixture(hp=800,ready=1,rate=1,now=60,reserved=0,pool_empty=False):
        u=new(ident=1,target_owner=0,hp=hp);unit(u,caster2,108,player,1000,512,properties=1);unit(u,friend,14,player,1000,512)
        put(u,caster2+0xd0,friend);put(u,caster2+0xd4,ready);put(u,0x665b08+player*4,caster2);put(u,friend+0x80,caster2)
        put(u,0x534104+player*24+1,0,1);put(u,p+0x212,rate,1);put(u,0x5349d8,now);put(u,target+0x92,reserved,1)
        if pool_empty:put(u,0x5aef00,0);put(u,0x5aef04,0)
        spatial_indices(u,[caster,target]);return u
    for hp,ready,now,reserved,pool_empty in((799,1,60,0,False),(800,1,60,0,False),(800,0,60,0,False),(800,1,59,0,False),(800,1,60,1,False),(800,1,60,0,True)):
        u=nuclear_fixture(hp,ready,now=now,reserved=reserved,pool_empty=pool_empty);ex=call(u,0x48d3a0,edx=1)
        selected=hp>=800 and ready and now>=60 and not reserved;issued=selected and not pool_empty
        record('full Ghost decision composes Nuke readiness rate score reservation primary issue attachment and timestamp',{'enemy_hp':hp,'silo_ready':ready,'tick':now,'rate':1,'target_reserved':reserved,'pool_empty':pool_empty,'Lockdown_available':False},ex,
               [ex['return_eax'],get(u,caster+0x4d,1),get(u,caster+0x5c),get(u,target+0x92,1),get(u,friend+0x80),get(u,p+0x214),get(u,caster+0xa2,2)],
               [int(selected),126 if issued else 1,target if issued else 0,1 if selected else reserved,caster if selected else caster2,now if selected else 0,200*256])
    for oldtarget,visible,energy in((target,255,200),(friend,255,200),(0,0,200),(0,255,0)):
        u=nuclear_fixture();current=126 if oldtarget else 1;put(u,caster+0x4d,current,1);put(u,caster+0x5c,oldtarget);put(u,sprites+0x100+0xc,visible,1);put(u,caster+0xa2,energy*256,2)
        ex=call(u,0x48d3a0,edx=1)
        record('Nuke branch lacks ordinary same-target early exit and readiness energy gate and visibility test',{'initial_order':current,'initial_target':oldtarget,'sprite_visibility':visible,'energy':energy},ex,
               [ex['return_eax'],get(u,caster+0x4d,1),get(u,caster+0x5c),get(u,target+0x92,1),get(u,friend+0x80),get(u,p+0x214)],
               [1,126,oldtarget if current==126 else target,0 if current==126 else 1,caster,60])
    for neighbor_x in(624,656):
        u=new(target_id=146,target_owner=0,hp=1,properties=1);put(u,target+0xdc,0x20001);unit(u,friend,15,0,neighbor_x,512,hp=1000)
        spatial_indices(u,[caster,target,friend]);ex=call(u,0x4335b0,ecx=target,edx=caster)
        record('full Nuke neighborhood preserves enumeration-dependent hard assignment versus later life addition',{'special_building_id':146,'special_x':640,'nonbuilding_hp':1000,'nonbuilding_x':neighbor_x},ex,
               [ex['return_eax'],get(u,0x665d28)],[1,800 if neighbor_x<640 else 1800])
    # Recall region and own-unit target; original pixel-region helper runs.
    mapbase,regionbase,tiles,tileids,overlays,cv5,vf4=0xb00000,0x820000,0x814000,0x818000,0x81a000,0x81c000,0x81d000
    def terrain(u):
        u.mem_map(mapbase,0x60000);put(u,0x69b584,mapbase);put(u,mapbase,2,2);put(u,0x6b1a74+player*4,regionbase)
        for y in range(32):
            for x in range(32):put(u,mapbase+0xc+(y*256+x)*2,1,2)
        put(u,0x5264b4,64,2);put(u,0x5264b6,64,2);put(u,0x666454,tiles);put(u,0x666458,tileids);put(u,0x66696c,overlays);put(u,0x6ce650,cv5);put(u,0x66644c,vf4)
        # No completed neutral DarkSwarm202: genuine native count guard prevents
        # the existing-field rectangle branch, not a replacement helper return.
        put(u,0x52c0c4+(202*12+11)*4,0)
    for state,air,ground,flag in((0,0,0,0),(3,0,0,0),(8,0,0,0),(0,1,0,0),(0,0,1,0),(0,0,0,0x20),(5,0,0,0)):
        u=new();terrain(u);r=regionbase+0x34;put(u,r+5,state,1);put(u,r+0x1c,air);put(u,r+0x20,ground);put(u,r+8,flag,1);ex=call(u,0x4aea70,ecx=player,edx=1)
        record('Recall region permission rejects state3 local air-ground target pointers or flag20',{'state':state,'air_target':air,'ground_target':ground,'flags':flag},ex,ex['return_eax'],int(state!=3 and not air and not ground and not flag&0x20))
    for ident,owner,attack,rawshields in((72,7,1,10*256),(72,7,1,10*256+255),(72,7,1,11*256),(72,7,0,0),(72,0,1,0),(82,7,1,0),(83,7,1,0),(81,7,1,0),(70,7,1,0)):
        u=new(ident=71,target_id=ident,target_owner=owner);put(u,target+0x5c,friend if attack else 0);put(u,target+0x60,rawshields);ex=call(u,0x433c10,ecx=target,edx=caster)
        record('Recall target exact carrier-reaver IDs and attacking depleted-shield predicate',{'target_id':ident,'owner':owner,'attack_target':attack,'raw_shields':rawshields},ex,ex['return_eax'],int(owner==player and attack and ident in(72,82,83,81)and(rawshields&0xffffff00)<=10*256))
    for region_blocked,shields,researched,reserved in((False,10,1,0),(True,10,1,0),(False,11,1,0),(False,10,0,0),(False,10,1,1)):
        u=new(ident=71,target_id=72);terrain(u);put(u,target+0x5c,friend);put(u,target+0x60,shields*256);put(u,regionbase+0x34+5,3 if region_blocked else 0,1)
        put(u,0x534104+player*24+22,0,1);put(u,0x534224+player*24+21,researched,1);put(u,target+0x92,reserved,1);ex=call(u,0x48d3a0,edx=1)
        ok=not region_blocked and shields<=10 and researched and not reserved
        record('full Arbiter decision composes real regional gate Recall own-carrier predicate technology permission and order',{'region_state3':region_blocked,'target_shields':shields,'Recall_researched':researched,'reserved':reserved,'Stasis_available':False},ex,
               [ex['return_eax'],get(u,caster+0x4d,1),get(u,caster+0x5c),get(u,target+0x92,1)],
               [int(ok),137 if ok else 1,target if ok else 0,1 if ok else reserved])
    # DarkSwarm: select an enemy attacking an allied melee ground unit. A flying
    # selected enemy changes the final spell target to that enemy's attack target.
    def swarm_fixture(enemy_flying=False,enemy_behavior=1,ally_behavior=5,tile_miss=False,enemy_elevation=0,ally_elevation=0):
        u=new(ident=46,target_owner=0);terrain(u);put(u,target+0x5c,friend);put(u,target+0xdc,5 if enemy_flying else 1)
        put(u,0x5211d8,0,1);put(u,0x5257d0,enemy_behavior,1);put(u,0x5211d8+15,1,1);put(u,0x5257d0+1,ally_behavior,1)
        if tile_miss:put(u,tiles+(16*64+20)*4,0x100000)
        # Separate megatiles for the two positions permits genuine height checks.
        put(u,tileids+(16*64+20)*2,1,2);put(u,tileids+(16*64+20)*2+2,2,2);put(u,cv5+0x14+2,1,2);put(u,cv5+0x14+4,2,2)
        # target640 and friend656 share one32pixel tile, but distinct8pixel minitiles.
        put(u,vf4+(1*16+0)*2,4 if enemy_elevation==2 else enemy_elevation*2,2)
        put(u,vf4+(1*16+2)*2,4 if ally_elevation==2 else ally_elevation*2,2)
        spatial_indices(u,[caster,target,friend]);return u
    for flying,enemy_behavior,ally_behavior,tile_miss in((False,1,5,False),(True,1,5,False),(False,5,5,False),(False,3,5,False),(False,7,5,False),(False,1,1,False),(False,1,5,True)):
        u=swarm_fixture(flying,enemy_behavior,ally_behavior,tile_miss);ex=call(u,0x4331d0,edx=1);ok=enemy_behavior in(0,1,2,4,7)and ally_behavior==5 and not tile_miss
        record('complete DarkSwarm selector executes enemy attack melee projectile terrain-miss gates and flying retargeting',{'enemy_flying':flying,'enemy_weapon_behavior':enemy_behavior,'ally_weapon_behavior':ally_behavior,'ally_tile_miss':tile_miss},ex,ex['return_eax'],(friend if flying else target)if ok else 0)
    for flying in(False,True):
        u=swarm_fixture(flying);put(u,0x534104+player*24+15,0,1);ex=call(u,0x48d3a0,edx=1);chosen=friend if flying else target
        record('full Defiler decision reaches DarkSwarm fallback and reserves the actual redirected target',{'enemy_flying':flying,'Plague_available':False,'selected_target':chosen},ex,
               [ex['return_eax'],get(u,caster+0x4d,1),get(u,caster+0x5c),get(u,target+0x92,1),get(u,friend+0x92,1),get(u,caster+0xa2,2)],
               [1,119,chosen,0 if flying else 1,1 if flying else 0,200*256])
    for enemy_height,ally_height in((0,0),(0,1),(0,2),(1,0),(1,1),(1,2),(2,0),(2,1),(2,2)):
        u=swarm_fixture(enemy_elevation=enemy_height,ally_elevation=ally_height);ex=call(u,0x4331d0,edx=1)
        record('DarkSwarm eligibility consumes real terrain height comparison in projectile miss helper',{'enemy_height':enemy_height,'ally_height':ally_height,'no_tile_miss':True},ex,ex['return_eax'],target if enemy_height>=ally_height else 0)
    for field_id,field_x,field_owner,field_completed in((202,656,11,1),(202,800,11,1),(201,656,11,1),(202,656,7,0)):
        u=swarm_fixture();unit(u,caster2,field_id,field_owner,field_x,512,hp=1,status=field_completed)
        for ident in(15,field_id):u.mem_write(0x522c38+ident*8,struct.pack('<4H',8,8,8,8))
        put(u,0x52c0c4+(202*12+11)*4,1);put(u,0x5d6e40,16);put(u,0x5d6e44,16);spatial_indices(u,[caster,target,caster2,friend])
        ex=call(u,0x4331d0,edx=1);overlap=field_id==202 and field_x==656
        record('DarkSwarm selector executes positive real existing-field overlap query with restored spatial scratch stack',{'counter_completed202_owner11':1,'field_id':field_id,'field_x':field_x,'field_owner':field_owner,'field_completed':field_completed},ex,
               [ex['return_eax'],get(u,0x5d6e48),get(u,0x5d6e5c)],[0 if overlap else target,0,0])
    def disable(u,*technologies):
        for techid in technologies:
            put(u,(0x534104+player*24+techid)if techid<24 else(0x536330+player*20+techid-24),0,1)
    def decision(label,inputs,u,order,ok,reactive=1,mask=1):
        ex=call(u,0x48d3a0,edx=reactive)
        record(label,inputs,ex,[ex['return_eax'],get(u,caster+0x4d,1),get(u,caster+0x5c),get(u,target+0x92,1)],
               [int(ok),order if ok else 1,target if ok else 0,mask if ok else 0])
        return ex
    for hp in(199,200,450,451):
        u=new(ident=12,target_owner=0,hp=hp)
        decision('full Battlecruiser Yamato decision consumes inclusive ordinary-target life band',{'enemy_hp':hp,'target_properties':0},u,113,200<=hp<=450)
    for locked,researched in((0,1),(1,1),(0,0)):
        u=new(ident=1,target_id=72,target_owner=0,properties=0x40000000);put(u,target+0x117,locked,1);put(u,0x534224+player*24+1,researched,1)
        decision('full Ghost Lockdown decision executes mechanical Carrier exception and stock technology permission',{'target_id':72,'Lockdown_timer':locked,'researched':researched,'no_silo_list':True},u,115,not locked and bool(researched))
    for energy in(149,150):
        u=new(ident=45,target_owner=0,hp=100,properties=0x10000);put(u,caster+0xa2,energy*256,2);disable(u,18,17)
        decision('full noncampaign Queen Broodlings decision reaches queued Move followup',{'caster_energy':energy,'enemy_organic_hp':100,'Parasite_and_Ensnare_available':False},u,121,energy>=150)
        node=get(u,caster+0x74)
        if energy>=150:assert node and get(u,node+8,1)==6 and get(u,node+0xc)==(512|(512<<16))
        cases[-1]['observed_followup_order']={'node':node,'order':get(u,node+8,1)if node else None,'position':get(u,node+0xc)if node else None}
    for hp in(299,300):
        u=new(ident=45,target_owner=0,hp=hp);disable(u,13,17)
        decision('full noncampaign full-energy Queen Parasite decision reserves mask4',{'enemy_hp':hp,'Queen_energy':200,'Broodlings_and_Ensnare_available':False},u,120,hp>=300,mask=4)
    for reactive,hp in((0,249),(0,250),(1,100)):
        u=new(ident=67,target_owner=0,hp=hp);disable(u,20)
        decision('full HighTemplar Storm decision uses shared reactive area threshold',{'reactive':reactive,'enemy_hp':hp,'Hallucination_available':False},u,142,bool(reactive)or hp>=250,reactive=reactive)
    for hp,shared in((249,0),(250,0),(100,1)):
        u=new(ident=71,target_owner=0,hp=hp,properties=0x10000);put(u,target+0x5c,friend);put(u,0x665d30,shared);disable(u,21)
        decision('full Arbiter Stasis decision consumes shared threshold state separately from reactive range argument',{'enemy_organic_hp':hp,'shared_reactive':shared,'argument_reactive':1,'attack_target_owner':player,'Recall_available':False},u,147,bool(shared)or hp>=250)
    for target_energy in(99,100):
        u=new(ident=63,target_id=1,target_owner=0,hp=100,properties=0x200000);put(u,target+0xa2,target_energy*256,2);disable(u,27,31)
        decision('full DarkArchon Feedback decision compares enemy integer energy with rounded life',{'enemy_spellcaster_hp':100,'enemy_energy':target_energy,'MindControl_and_Maelstrom_available':False},u,184,target_energy>=100)
    u=new(ident=63,target_id=12,target_owner=0);disable(u,29)
    decision('full DarkArchon MindControl decision selects preferred Battlecruiser',{'target_id':12,'Feedback_available':False},u,182,True)
    for energy in(199,200):
        u=new(ident=63,target_owner=0,hp=100,properties=0x10000);put(u,target+0x5c,friend);put(u,caster+0xa2,energy*256,2);disable(u,29,27)
        decision('full DarkArchon Maelstrom decision retains MindControl-cost energy reserve even when unavailable',{'caster_energy':energy,'enemy_organic_hp':100,'Feedback_and_MindControl_available':False},u,186,energy>=200)
    for hp in(60,61):
        u=new(ident=34,target_owner=player,hp=hp);put(u,target+0x116,1,1);disable(u,30)
        decision('full Medic Restoration decision consumes harmful timer and strict life threshold',{'own_hp':hp,'harmful_timer116':1,'OpticalFlare_available':False},u,180,hp>60)
    for hp in(80,81):
        u=new(ident=34,target_owner=0,hp=hp);disable(u,24)
        decision('full full-energy Medic OpticalFlare decision consumes strict ordinary-target life threshold',{'enemy_hp':hp,'Medic_energy':200,'Restoration_available':False},u,185,hp>80)
    for organic in(False,True):
        u=new(ident=9,target_owner=0,hp=100,properties=0x10000 if organic else 0);disable(u,2,6)
        decision('full ScienceVessel Irradiate decision executes real organic nonbuilding predicate',{'enemy_organic':organic,'EMP_and_Matrix_available':False},u,143,organic)
    for shields in(199,200):
        u=new(ident=9,target_id=65,target_owner=0,hp=100);put(u,target+0x60,shields*256);put(u,0x520458+65,1,1);disable(u,6,7)
        decision('full ScienceVessel EMP decision consumes neighborhood shield threshold',{'enemy_shields':shields,'Matrix_and_Irradiate_available':False},u,122,shields>=200)
    u=new(ident=60,target_id=124,target_owner=0,hp=100,properties=1);put(u,target+0xdc,3);put(u,target+0x5c,friend);put(u,friend+0xdc,5)
    decision('full Corsair Web decision executes original bounding-box range helper for ground anti-air target',{'enemy_id':124,'enemy_attack_target_flying':True,'enemy_center_distance':128},u,181,True)
    for status,field in((1,None),(0,None),(0x401,None),(1,0x117),(1,0x119),(1,0x124),(1,0x123)):
        u=new(target_id=124,properties=0x8001);put(u,target+0xdc,status)
        if field is not None:put(u,target+field,1,1)
        ex=call(u,0x431a90,ecx=target)
        record('active detector helper uses completed disabled lockdown stasis maelstrom and blind gates',{'status':status,'nonzero_byte_offset':hex(field)if field is not None else None,'properties':0x8001},ex,ex['return_eax'],int(status==1 and field is None))
    for desired in(0,1,2,255,257):
        seed=next(s for s in range(1000000)if ((((s*0x15a4e35+1)&0xffffffff)>>16)&0x7fff)==desired)
        u=new(ident=45,target_owner=0,hp=300,properties=0x10000);put(u,p+0x218,0x20,2);put(u,0x673930,1);put(u,0x67393c,seed);put(u,0x673938,0);put(u,0x673940+42*4,0)
        order=120 if desired&255==1 else 121
        decision('full campaign Queen chooses Parasite only when original source42 RNG low byte equals one',{'current_seed':seed,'expected_rng15':desired,'campaign_flag20':True,'both_primary_spells_available':True},u,order,True,mask=4 if order==120 else 1)
        rng_observed=[get(u,0x67393c),get(u,0x673938),get(u,0x673940+42*4)]
        rng_expected=[(seed*0x15a4e35+1)&0xffffffff,1,1]
        assert rng_observed==rng_expected
        cases[-1]['original_rng_state_and_counters_observed']=rng_observed
        cases[-1]['original_rng_state_and_counters_expected']=rng_expected
    for reserved,parasite in((0,0),(1,0),(4,0),(5,0),(0,1),(0,128)):
        u=new(ident=45,target_owner=0,hp=300);disable(u,13,17);put(u,target+0x92,reserved,1);put(u,target+0x121,parasite,1)
        ok=not reserved&4 and not parasite&128;ex=call(u,0x48d3a0,edx=1)
        record('full Parasite target predicate and independent reservation bit preserve other spell reservations',{'target_reservation':reserved,'target_parasite_owner_mask':parasite,'caster_owner':7,'Broodlings_and_Ensnare_available':False},ex,
               [ex['return_eax'],get(u,caster+0x4d,1),get(u,target+0x92,1)],[int(ok),120 if ok else 1,reserved|4 if ok else reserved])
    for hp,shared,bit20000 in((249,0,True),(250,0,True),(100,1,True),(250,1,False)):
        u=new(ident=45,target_owner=0,hp=hp);disable(u,13,18);put(u,target+0xdc,0x20001 if bit20000 else 1);put(u,target+0x5c,friend);put(u,0x665d30,shared)
        decision('full Queen Ensnare fallback consumes exact status bit20000 and inherited area threshold',{'enemy_hp':hp,'status_bit20000':bit20000,'shared_reactive':shared,'argument_reactive':1,'Broodlings_and_Parasite_available':False},u,146,bit20000 and(bool(shared)or hp>=250))
    for center_delta,limit in((0,0),(16,0),(17,0),(18,0),(128,110),(128,111),(273,256),(274,256)):
        u=new();put(u,sprites+0x100+0x14,512+center_delta,2)
        for ident in(9,0):u.mem_write(0x522c38+ident*8,struct.pack('<4H',8,8,8,8))
        ex=call(u,0x4229a0,args=(limit,target));gap=max(center_delta-17,0)
        record('Web range helper compares inclusive approximate distance between half-open original bounding rectangles',{'horizontal_center_delta':center_delta,'all_extents':8,'range_limit':limit},ex,ex['return_eax'],int(gap<=limit))
    for existing in(False,True):
        u=swarm_fixture();put(u,0x534104+player*24+15,0,1)
        if existing:
            unit(u,caster2,202,11,656,512,hp=1);u.mem_write(0x522c38+15*8,struct.pack('<4H',8,8,8,8));u.mem_write(0x522c38+202*8,struct.pack('<4H',8,8,8,8))
            put(u,0x52c0c4+(202*12+11)*4,1);put(u,0x5d6e40,16);put(u,0x5d6e44,16);spatial_indices(u,[caster,target,caster2,friend])
        decision('full DarkSwarm decision consumes genuine overlapping existing-field query before reservation and order',{'overlapping_DarkSwarm202':existing,'Plague_available':False},u,119,not existing)
    return {'format_version':1,'original_code_executed':True,'complete_game_executed':False,'windows_or_host_api_called':False,'helper_stubs':[],'all_matched':True,
            'case_count':len(cases),'complete_return_count':len(cases),'prefix_case_count':0,'cases':cases,
            'allowlisted_preferred_va_ranges':[[hex(a),hex(b)]for a,b in allow],
            'runtime_versions':{'python':platform.python_version(),'unicorn':unicorn.__version__,'capstone':capstone.__version__,'pefile':pefile.__version__,'architecture':platform.machine(),'system':platform.system()}}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in('exe','orders-dat','weapons-dat','techdata-dat','out'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--execute-isolated-original-code',action='store_true');p.add_argument('--probe-out',type=Path);a=p.parse_args()
    if a.execute_isolated_original_code and not a.probe_out:p.error('Execution requires --probe-out')
    raw=a.exe.read_bytes();orders_raw=a.orders_dat.read_bytes();weapons=a.weapons_dat.read_bytes();tech=a.techdata_dat.read_bytes()
    assert hashlib.sha256(raw).hexdigest()==TARGET_HASH
    dat=dat_profile(orders_raw,weapons,tech);orders=primary.stock_orders(orders_raw);pe=pefile.PE(data=raw)
    assert pe.OPTIONAL_HEADER.ImageBase==0x400000 and pe.FILE_HEADER.Machine==0x14c
    md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);sha=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    result={'target_sha256':TARGET_HASH,'target_version':'1.17.0.1','inspector_sha256':sha,'dependency_script_sha256':dependencies(),'original_code_executed':False,'witnesses':{}}
    for name,(lo,hi)in WITNESSES.items():
        code=pe.get_data(lo-0x400000,hi-lo);ins=list(md.disasm(code,lo));assert sum(i.size for i in ins)==len(code),name
        result['witnesses'][name]={'start_preferred_va':hex(lo),'end_exclusive_preferred_va':hex(hi),'bytes_sha256':hashlib.sha256(code).hexdigest(),'instructions':[{'address':hex(i.address),'bytes':i.bytes.hex(),'mnemonic':i.mnemonic,'operands':i.op_str}for i in ins]}
    ts=struct.unpack('<2I',pe.get_data(0x433f10-0x400000,8));result['DarkSwarm_enemy_weapon_behavior_switch']={str(k):hex(ts[j])for k,j in enumerate(pe.get_data(0x433f18-0x400000,8))}
    result['stock_dat_hashes']={'orders':primary.ORDERS_HASH,'weapons':WEAPONS_HASH,'techdata':TECH_HASH}
    a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n')
    if a.execute_isolated_original_code:
        run=execute(pe,orders,dat);run.update({'target_sha256':TARGET_HASH,'target_version':'1.17.0.1','probe_script_sha256':sha,'dependency_script_sha256':dependencies(),'stock_dat_hashes':result['stock_dat_hashes']})
        a.probe_out.parent.mkdir(parents=True,exist_ok=True);a.probe_out.write_text(json.dumps(run,indent=2)+'\n');print(json.dumps({'cases':run['case_count'],'complete':run['complete_return_count'],'sha256':sha}))
    else:print('Wrote static spell-family evidence; original code not executed')

if __name__=='__main__':main()
