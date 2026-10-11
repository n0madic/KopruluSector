#!/usr/bin/env python3
"""Recover spell permission, target validity and original decision/issue paths.

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

TARGET_HASH = combat.TARGET_HASH
WEAPONS_HASH = '80de165015519f9bed46ea8858195c2d2d0120b65b9e55e1c0a50b19135f4b62'
TECH_HASH = '98f9805ebc8efc707492b7406871298d39d6e94931458aca1f82feaf18a271af'
WITNESSES = {
    'weapon_target_validity': (0x41DBF0,0x41DCD6),
    'technology_target_validity': (0x434130,0x434243),
    'technology_unit_target_error': (0x435380,0x43548D),
    'technology_fog_type_error': (0x435320,0x435357),
    'type_not_invincible': (0x420A90,0x420AB0),
    'nonhallucinated_spellcaster': (0x4118D0,0x4118F6),
    'completed_damaged_command_center': (0x41FED0,0x41FF23),
    'race_from_group': (0x428D60,0x428D83),
    'maximum_hp': (0x409C90,0x409CB9),
    'enemy_relationship': (0x4290E0,0x429110),
    'native_unit_pool': (0x4255F0,0x4255F6),
    'technology_tile_mask': (0x413220,0x41326B),
    'maximum_energy': (0x434940,0x4349D6),
    'maximum_energy_high_byte': (0x434A80,0x434A89),
}

def dependencies():
    return {Path(m.__file__).name: hashlib.sha256(Path(m.__file__).read_bytes()).hexdigest()
            for m in (combat, requirements, primary, spatial)}

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
    allow=list(WITNESSES.values())+list(requirements.WITNESSES.values())+list(primary.BLOCKS.values())+list(spatial.BLOCKS.values())
    allow += [combat.BLOCKS[k] for k in ('approximate_distance','cloak_detection_rejection','rounded_life','order_energy_cost','energy_insufficient',
              'spell_candidate_callbacks_a1','spell_candidate_callbacks_a2','spell_selectors_a','spell_selectors_b','spell_candidate_callbacks_b1',
              'spell_candidate_callbacks_b2','spell_candidate_callbacks_b3','spell_candidate_callbacks_c','spell_candidate_callbacks_d1','spell_candidate_callbacks_d2',
              'spell_area_enemy_life_score','spell_area_allied_life_score','maelstrom_area_enemy_life_score','radius_callback_iteration',
              'nearest_rectangle_search_setup','nearest_rectangle_search_loop','campaign_ai_flag','global_spell_cooldown_zero',
              'reaction_spell_dispatch','spell_ready','reserve_spell_target')]
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
        spatial_indices(u,[caster,target]);return u
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
    # Numeric flag predicates use controlled DAT target flags, separately from stock spell chains below.
    for flags in(0,1,2,3,7,11,19,35,67,131,259,0x1ff):
        for status,props,owner in((1,0,7),(5,0,7),(1,0x40000000,7),(1,0x10000,7),(1,0x4000,7),(1,1,7),(1,0x40010000,0),(0x4000001,0,7)):
            u=new(properties=props,target_owner=owner);put(u,target+0xdc,status);put(u,0x5251a8+32*2,flags,2)
            expected=bool(not(status&0x4000000)and flags&(1 if status&4 else 2)and(not flags&4 or props&0x40000000)and(not flags&0x20 or not props&0x4000)and(not flags&8 or props&0x10000)and(not flags&0x10 or not props&1)and(not flags&0x80 or props&0x40010000)and(not flags&0x100 or owner==player))
            ex=call(u,0x41dbf0,args=(32,target));record('weapon flags combine target class and ownership predicates',{'flags':flags,'target_status':status,'target_properties':props,'target_owner':owner},ex,ex['return_eax'],int(expected))
    for wid in(0,32,129,130,255,256):
        for flags in(3,0x40,0x43):
            u=new();put(u,0x5251a8+(wid&255)*2,flags,2);ex=call(u,0x41dbf0,args=(wid,0))
            record('null target returns terrain mask and weapon operand truncates to byte',{'weapon_argument':wid,'flags_at_byte_id':flags,'null_target':True},ex,ex['return_eax'],flags&0x40 if(wid&255)<130 else 0)
    error_out=images+0x8000
    bypass={2,4,14,15,17,19,21,22,25,31}
    for techid in range(44):
        u=new();put(u,error_out,0x7777,2)
        # Tech3 uses the real signed pixel-to-tile mask helper.
        put(u,0x5264b4,64,2);put(u,0x666454,images+0x9000);put(u,images+0x9000+(16*64+20)*4,0x410000)
        ex=call(u,0x434130,args=(techid,target,228,640,512,error_out))
        err=0 if techid in bypass|{3,6,7,20,24,30} else 0x366 if techid==12 else 0x381 if techid==16 else 0x52f if techid==27 else 0x532 if techid==29 else 0x36c
        record('full technology target dispatch uses original switches and actual helpers',{'technology':techid,'same_owner_nonbuilding':True,'target_id':0,'target_properties':0,'tile_mask':0x410000},ex,
               [ex['return_eax'],get(u,error_out,2)],[int(err==0),err or 0x7777])
    for techid in(6,7,20,24,30):
        for stasis,building,invincible in((0,0,0),(1,0,0),(0,1,0),(0,0,1)):
            u=new(properties=1 if building else 0);put(u,target+0x119,stasis,1);put(u,target+0xdc,1|(0x4000000 if invincible else 0));put(u,error_out,0x7777,2)
            ex=call(u,0x434130,args=(techid,target,228,640,512,error_out))
            err=0x36c if invincible else 0x384 if stasis else 0x36d if building else 0
            record('Matrix Irradiate Hallucination Restoration Flare reject stasis buildings and untargetable entities',{'technology':techid,'stasis':stasis,'building_properties':building,'status_untargetable':invincible},ex,[ex['return_eax'],get(u,error_out,2)],[int(err==0),err or 0x7777])
    for techid in(6,12,16,20,29):
        for fogtype,props in((228,0),(106,0),(0,0),(106,0x20000000),(65535,0)):
            u=new();put(u,0x521660+106*4,props);put(u,error_out,0x7777,2)
            ex=call(u,0x434130,args=(techid,0,fogtype,640,512,error_out))
            err=0x36e if fogtype==228 else 0x36c if fogtype>=228 or props&0x20000000 else 0 if techid==12 and fogtype==106 else 0x366 if techid==12 else 0x36d if techid==6 else 0x381 if techid==16 else 0x36c
            record('technology fog type path validates type and uses separate error dispatch',{'technology':techid,'target':0,'fog_type':fogtype,'type106_properties':props},ex,[ex['return_eax'],get(u,error_out,2)],[int(err==0),err or 0x7777])
    for tile in(0,0x10000,0x400000,0x410000,0xffffffff):
        u=new();put(u,0x5264b4,64,2);put(u,0x666454,images+0x9000);put(u,images+0x9000+(16*64+20)*4,tile);put(u,error_out,0x7777,2)
        ex=call(u,0x434130,args=(3,0,228,640,512,error_out));ok=bool(tile&0x410000)
        record('Nuclear technology3 validates coordinate tile mask before object requirements',{'technology':3,'tile_flags':tile,'pixels':[640,512]},ex,[ex['return_eax'],get(u,error_out,2)],[int(ok),0x7777 if ok else 0x386])
    for techid in sorted(bypass):
        for nulltarget,untargetable in((True,False),(False,True)):
            u=new();put(u,target+0xdc,1|(0x4000000 if untargetable else 0));put(u,error_out,0x7777,2)
            ex=call(u,0x434130,args=(techid,0 if nulltarget else target,228,640,512,error_out))
            record('position spell technology bypass returns before object and untargetable target checks',{'technology':techid,'null_target':nulltarget,'untargetable_target':untargetable},ex,[ex['return_eax'],get(u,error_out,2)],[1,0x7777])
    for techid,ident,owner,props,group,hp,maxhp,expected in(
        (29,0,7,0x200000,0,50,100,0),(29,0,7,0,0,50,100,0x532),
        (27,0,0,0,0,50,100,0),(27,0,7,0,0,50,100,0x52f),(27,0,0,1,0,50,100,0x36d),
        (16,0,7,0,1,50,100,0),(16,35,7,0,1,50,100,0x381),(16,0,0,0,1,50,100,0x381),
        (12,106,7,1,2,49,100,0),(12,106,7,1,2,50,100,0x366),(12,106,7,1,2,0,0,0)):
        u=new(target_id=ident,target_owner=owner,hp=hp,properties=props);put(u,0x523c40+ident,group,1);put(u,0x5208b0,maxhp*256);put(u,error_out,0x7777,2)
        ex=call(u,0x434130,args=(techid,target,228,640,512,error_out))
        record('positive and boundary technology object-specific predicates execute actual callees',{'technology':techid,'target_id':ident,'owner':owner,'properties':props,'race_group':group,'hp':hp,'command_center_maxhp':maxhp},ex,[ex['return_eax'],get(u,error_out,2)],[int(expected==0),expected or 0x7777])
    # Permission consumes unchanged lazy native technology recipes, stock costs/order columns.
    for order,techid,ident in((141,6,9),(113,8,12),(142,19,67),(148,20,67),(144,15,46)):
        cost=dat['energy'][techid]*256
        for delta,researched,available,disabled,hallucinated in((-1,1,1,0,0),(0,1,1,0,0),(1,1,1,0,0),(0,0,1,0,0),(0,1,0,0,0),(0,1,1,1,0),(0,1,1,0,1)):
            u=new(ident=ident);put(u,caster+0xa2,cost+delta,2);put(u,caster+0xdc,1|(0x400 if disabled else 0)|(0x40000000 if hallucinated else 0))
            put(u,(0x534224+player*24+techid)if techid<24 else(0x536420+player*20+techid-24),researched,1)
            put(u,(0x534104+player*24+techid)if techid<24 else(0x536330+player*20+techid-24),available,1)
            ex=call(u,0x48e580,edx=order,args=(techid,));ok=delta>=0 and (researched or techid==6) and available and not disabled and not hallucinated
            record('spell readiness composes stock energy cost with original lazy technology permission recipe',{'order':order,'technology':techid,'caster_id':ident,'stock_energy_cost':cost,'delta':delta,'researched':researched,'available':available,'disabled':disabled,'hallucinated':hallucinated},ex,ex['return_eax'],int(bool(ok)))
    # Matrix callback and selector then full Science Vessel dispatcher -> real issuance.
    for hp,maxhp,stasis,building,attack in((50,100,0,0,1),(75,100,0,0,1),(76,100,0,0,1),(50,99,0,0,1),(50,100,1,0,1),(50,100,0,1,1),(50,100,0,0,0)):
        u=new(hp=hp,properties=int(building));put(u,0x520708,maxhp*256);put(u,target+0x5c,caster2 if attack else 0);put(u,target+0x119,stasis,1)
        ex=call(u,0x4328d0,ecx=target,edx=caster);ok=hp<=75 and maxhp>=100 and not stasis and not building and attack
        record('Matrix candidate callback completes target technology6 helper and exact life gates',{'hp':hp,'maxhp':maxhp,'stasis':stasis,'building_properties':building,'has_attack_target':attack},ex,ex['return_eax'],int(bool(ok)))
    u=new();put(u,target+0x5c,caster2);ex=call(u,0x432c70,edx=1)
    record('complete Matrix nearest selector finds real spatial pool target',{'caster_id':9,'reactive':1,'spatial_units':[caster,target]},ex,ex['return_eax'],target)
    for current,reserved,reactive in((1,0,1),(141,0,1),(1,1,1),(1,0,0)):
        u=new();put(u,target+0x5c,caster2);put(u,caster+0x4d,current,1);put(u,caster+0x5c,target if current==141 else 0);put(u,target+0x92,reserved,1)
        # EMP unavailable so Matrix is the positive second branch; Irradiate unavailable.
        for t in(2,7):put(u,0x534104+player*24+t,0,1)
        ex=call(u,0x48d3a0,edx=reactive);success=current!=141 and not reserved
        record('Science Vessel full native decision permission selection reservation and primary order chain',{'current_order':current,'target_reserved_mask':reserved,'reactive':reactive,'EMP_and_Irradiate_available':False},ex,
               [ex['return_eax'],get(u,caster+0x4d,1),get(u,caster+0x5c),get(u,target+0x92,1),get(u,caster+0xa2,2)],
               [int(success),141 if success else current,target if success or current==141 else 0,1 if success else reserved,200*256])
    for ident in(72,70,71,68,73,0):
        u=new(ident=67,target_id=ident);ex=call(u,0x433260,ecx=target,edx=caster)
        record('Hallucination target callback executes full target technology20 validity then exact preferred IDs',{'target_id':ident,'same_owner':True,'nonbuilding':True},ex,ex['return_eax'],int(ident in(72,70,71,68)))
    for energy,current,reserved in((200,1,0),(199,1,0),(200,148,0),(200,1,1)):
        u=new(ident=67,target_id=72);put(u,caster+0xa2,energy*256,2);put(u,caster+0x4d,current,1);put(u,caster+0x5c,target if current==148 else 0);put(u,target+0x92,reserved,1)
        put(u,0x534104+player*24+19,0,1);ex=call(u,0x48d3a0,edx=0);ok=energy==200 and current!=148 and not reserved
        record('Templar Hallucination fallback composes native maximum energy permission target validity and issuance',{'energy_high_byte':energy,'current_order':current,'target_reserved_mask':reserved,'Storm_available':False,'reactive':0},ex,
               [ex['return_eax'],get(u,caster+0x4d,1),get(u,caster+0x5c),get(u,target+0x92,1),get(u,caster+0xa2,2)],
               [int(ok),148 if ok else current,target if ok or current==148 else 0,1 if ok else reserved,energy*256])
    def area_fixture(hp=100,allyhp=40,shields=0):
        u=new(ident=67,target_owner=0,hp=hp);put(u,friend+8,allyhp*256);put(u,target+0x60,shields*256)
        put(u,0x520458,1 if shields else 0,1);put(u,caster2+0xc,sprites+0x200);put(u,sprites+0x200+0x14,512,2);put(u,sprites+0x200+0x16,640,2)
        spatial_indices(u,[caster,target,caster2,friend]);return u
    for reactive,hp,allyhp in((0,249,0),(0,250,0),(1,100,50),(1,101,50),(1,1,0),(0,300,150),(0,301,150)):
        u=area_fixture(hp,allyhp);ex=call(u,0x4330a0,edx=reactive);ok=(reactive or hp>=250)and hp>allyhp*2
        record('complete Storm selector scores real enemy and ally spatial units with strict inequality and threshold',{'reactive':reactive,'enemy_hp':hp,'allied_hp':allyhp,'spatial_units':[caster,target,caster2,friend]},ex,
               [ex['return_eax'],get(u,0x665d30),get(u,0x665d28),get(u,0x665d2c)],
               [target if ok else 0,reactive,hp,allyhp if reactive or hp>=250 else 0])
    for first_reactive,next_reactive in((1,0),(0,1),(1,1),(0,0)):
        u=area_fixture();first=call(u,0x4330a0,edx=first_reactive);ex=call(u,0x433060,ecx=caster2,edx=next_reactive);ex['preceding_complete_other_caster_selector']=first
        record('different-caster Plague selector retains reactive score input written by prior Storm selector',{'Storm_caster':caster,'Storm_reactive':first_reactive,'Plague_caster':caster2,'Plague_reactive':next_reactive,'enemy_hp':100,'ally_hp':40},ex,
               [first['return_eax'],ex['return_eax'],get(u,0x665d30),get(u,0x665d28),get(u,0x665d2c)],
               [target if first_reactive else 0,target if first_reactive else 0,first_reactive,100,40 if first_reactive else 0])
    for shields in(0,200):
        u=area_fixture(100,40,shields);first=call(u,0x4330a0,edx=1);ex=call(u,0x433060,ecx=caster2,edx=0);ex['preceding_complete_other_caster_selector']=first
        record('cross-caster shared state persists while Plague counts enemy HP without shields',{'enemy_hp':100,'enemy_shields':shields,'ally_hp':40,'Storm_reactive':1,'Plague_reactive':0},ex,[ex['return_eax'],get(u,0x665d30),get(u,0x665d28),get(u,0x665d2c)],[target,1,100,40])
    for inherited in(0,1):
        u=area_fixture();preceding=call(u,0x4330a0,edx=inherited)
        # The Defiler dispatcher argument is zero in both cases. Only the prior
        # other-caster selector changes the shared input read by Plague.
        put(u,0x534104+player*24+14,0,1);ex=call(u,0x48d3a0,ecx=caster2,edx=0);ex['preceding_complete_other_caster_selector']=preceding
        record('full Defiler decision inherits cross-caster score state then reserves and issues Plague',{'dispatcher_reactive':0,'prior_other_caster_Storm_reactive':inherited,'enemy_hp':100,'ally_hp':40,'DarkSwarm_available':False},ex,
               [ex['return_eax'],get(u,caster2+0x4d,1),get(u,caster2+0x5c),get(u,target+0x92,1),get(u,0x665d30),get(u,caster2+0xa2,2)],
               [inherited,144 if inherited else 1,target if inherited else 0,inherited,inherited,200*256])
    # Reservation itself accepts issuer return even when allocation fails or
    # an older noninterruptible queue head keeps the requested spell inactive.
    for pool_count in(0,1,20):
        u=new();put(u,0x5aef00,pool if pool_count else 0);put(u,0x5aef04,pool+(pool_count-1)*20 if pool_count else 0)
        if pool_count:put(u,pool+(pool_count-1)*20+4,0)
        ex=call(u,0x48e5c0,edx=141,args=(target,1));ok=pool_count>0
        record('spell reservation reports success and sets target bit despite native order allocation failure',{'pool_nodes':pool_count,'initial_order':1,'requested_order':141,'reservation_mask':1},ex,
               [ex['return_eax'],get(u,target+0x92,1),get(u,caster+0x4d,1),get(u,caster+0x5c)],
               [1,1,141 if ok else 1,target if ok else 0])
    for pool_count in(1,20):
        u=new();put(u,0x5aef04,pool+(pool_count-1)*20);put(u,pool+(pool_count-1)*20+4,0)
        prior=call(u,0x4295d0,args=(83,0,0,228,0));ex=call(u,0x48e5c0,edx=141,args=(target,1));ex['preceding_complete_old_head_enqueue']=prior
        head=get(u,caster+0x74)
        record('spell reservation accepts retained native stock HarvestGas83 head instead of immediately active Matrix',{'pool_nodes':pool_count,'stock_old_queue_order':83,'stock_old_interruptible':0,'requested_order':141},ex,
               [ex['return_eax'],get(u,target+0x92,1),get(u,caster+0x4d,1),get(u,caster+0xdc),get(u,head+8,1)if head else 0,get(u,head+16)if head else 0],
               [1,1,83,0x1001,141 if pool_count>1 else 0,target if pool_count>1 else 0])
    for mask in(1,4):
        for reserved,current,oldtarget in((0,1,0),(mask,1,0),(0,141,friend),(0,0,0)):
            u=new();put(u,target+0x92,reserved,1);put(u,caster+0x4d,current,1);put(u,caster+0x5c,oldtarget)
            ex=call(u,0x48e5c0,edx=141,args=(target,mask));rejected=bool(reserved&mask);issued=not rejected and current not in(0,141)
            record('reservation mask and preexisting order distinguish success from retargeting and active issuance',{'mask':mask,'reserved':reserved,'initial_order':current,'initial_target':oldtarget,'selected_target':target},ex,
                   [ex['return_eax'],get(u,target+0x92,1),get(u,caster+0x4d,1),get(u,caster+0x5c)],
                   [int(not rejected),reserved if rejected or current==141 else reserved|mask,141 if issued else current,target if issued else oldtarget])
    for pool_empty,current,oldtarget in((True,1,0),(False,141,friend)):
        u=new();put(u,target+0x5c,caster2);put(u,caster+0x4d,current,1);put(u,caster+0x5c,oldtarget)
        if pool_empty:put(u,0x5aef00,0);put(u,0x5aef04,0)
        for t in(2,7):put(u,0x534104+player*24+t,0,1)
        ex=call(u,0x48d3a0,edx=1)
        record('full Vessel dispatcher reports successful attempt despite absent order or existing different target',{'order_pool_empty':pool_empty,'initial_order':current,'initial_target':oldtarget,'selected_target':target},ex,
               [ex['return_eax'],get(u,caster+0x4d,1),get(u,caster+0x5c),get(u,target+0x92,1)],
               [1,current,oldtarget,int(pool_empty)])
    for wid,flags in enumerate(dat['weapon_flags']):
        for flying in(False,True):
            u=new(properties=0x40010000);put(u,target+0xdc,5 if flying else 1);ex=call(u,0x41dbf0,args=(wid,target))
            record('all unchanged stock weapon target flags execute original air-ground validity',{'weapon':wid,'stock_flags':flags,'flying':flying,'target_properties':0x40010000,'same_owner':True},ex,ex['return_eax'],int(bool(flags&(1 if flying else 2))))
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
    def switch(first,t,m,n,c):
        ts=struct.unpack('<'+'I'*n,pe.get_data(t-0x400000,n*4));return {str(first+j):hex(ts[k])for j,k in enumerate(pe.get_data(m-0x400000,c))}
    result['technology_target_switch']=switch(2,0x434244,0x43424c,2,30)
    result['technology_unit_error_switch']=switch(6,0x435490,0x4354ac,7,25)
    result['technology_fog_error_switch']=switch(6,0x435358,0x435368,4,11)
    result['MindControl_excluded_unit_ids']=[int(k)for k,v in switch(13,0x4354c8,0x4354d0,2,85).items()if v=='0x435483']
    result['original_technology_permission_recipes']=[r for r in requirements.parse_tables(pe)['technology_use']['recipes']if r['id']in(2,6,7,8,15,19,20)]
    result['shared_score_reactive_references_in_reviewed_combat_blocks']=[{'block':name,'address':row['address'],'mnemonic':row['mnemonic'],'operands':row['operands']}for name,(lo,hi)in combat.BLOCKS.items()
        for row in [{'address':hex(i.address),'mnemonic':i.mnemonic,'operands':i.op_str}for i in md.disasm(pe.get_data(lo-0x400000,hi-lo),lo)]if '0x665d30'in row['operands']]
    result['stock_dat_fields']=dat
    result['stock_dat_hashes']={'orders':primary.ORDERS_HASH,'weapons':WEAPONS_HASH,'techdata':TECH_HASH}
    a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n')
    if a.execute_isolated_original_code:
        run=execute(pe,orders,dat);run.update({'target_sha256':TARGET_HASH,'target_version':'1.17.0.1','probe_script_sha256':sha,'dependency_script_sha256':dependencies(),'stock_dat_hashes':result['stock_dat_hashes']})
        a.probe_out.parent.mkdir(parents=True,exist_ok=True);a.probe_out.write_text(json.dumps(run,indent=2)+'\n');print(json.dumps({'cases':run['case_count'],'complete':run['complete_return_count'],'sha256':sha}))
    else:print('Wrote static spell evidence; original code not executed')

if __name__=='__main__':main()
