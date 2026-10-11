#!/usr/bin/env python3
"""Recover selected spell effect and order consumers in exact local1.17 code.

Default mode reads the EXE as data. Execution requires the explicit isolated flag,
reviewed actual-callee instruction bounds, and a validated x86_64 runtime.
"""
import argparse, hashlib, json, platform, struct
from pathlib import Path
import capstone, pefile
import inspect_native_combat_ai_117 as combat
import inspect_native_dat_requirements_117 as requirements
import inspect_native_primary_orders_117 as primary
import probe_native_placement_117 as spatial
import inspect_native_reaction_spells_117 as reaction
import inspect_native_spell_families_117 as families
import inspect_native_damage_117 as damage
import probe_native_damage_117 as damageprobe
import inspect_native_image_initialization_117 as imageinit
import inspect_native_unit_creation_117 as creation
import inspect_native_projectiles_117 as projectiles
TARGET_HASH = combat.TARGET_HASH
WITNESSES = {
 'debit_energy':(0x435300,0x43531E),
 'apply_matrix':(0x438900,0x43895D),
 'apply_irradiate':(0x438960,0x4389B5),
 'clear_irradiate':(0x438620,0x43864C),
 'add_bottom_overlay':(0x4152D0,0x415335),
 'add_cast_overlay':(0x4151F0,0x415256),
 'emp_area_effect':(0x433FC0,0x4340C1),
 'recall_order':(0x42FC40,0x42FD70),
 'allocate_lone_sprite':(0x417B80,0x417BF4),
 'coordinate_sound':(0x446A20,0x446B2C),
 'matrix_order':(0x438560,0x4385C9),
 'check_unit_spell_and_range':(0x434B80,0x434C5F),
 'spell_target_range_and_facing':(0x4350B0,0x4351DD),
 'current_target_visibility':(0x422D10,0x422D86),
 'range_with_braking':(0x421120,0x42117F),
 'set_attack_waypoint':(0x40CBD0,0x40CBF7),
 'dark_swarm_create':(0x43DC10,0x43DC90),
 'recall_candidate':(0x4302C0,0x430317),
 'spell_timer_service':(0x4351E0,0x4352F7),
 'irradiate_damage_search':(0x4385D0,0x438619),
 'irradiate_damage_callback':(0x438650,0x4386D4),
 'acid_spore_timers':(0x43E280,0x43E3F7),
 'generic_spell_order':(0x434460,0x434695),
 'unit_is_dying':(0x41FE40,0x41FE55),
 'notify_invalid_target_nonlocal_gate':(0x446760,0x4467F6),
 'follow_target_with_wait_gate':(0x438BA0,0x438C11),
 'race_from_type_group':(0x414550,0x414574),
}
def dependencies():
 return {Path(m.__file__).name:hashlib.sha256(Path(m.__file__).read_bytes()).hexdigest() for m in (combat,requirements,primary,spatial,reaction,families,damage,damageprobe,imageinit,creation,projectiles)}
def dat_profile(orders,weapons,tech):
 d=families.dat_profile(orders,weapons,tech)
 d.update(cooldown=list(weapons[4160:4290]),damage=list(struct.unpack_from('<130H',weapons,3640)),inner_splash=list(struct.unpack_from('<130H',weapons,2860)),explosion=list(weapons[2730:2860]),order_weapon=list(orders[2646:2835]),order_animation=list(orders[3024:3213]))
 return d
def execute(pe, orders, dat):
    if platform.machine().lower() not in ('x86_64','amd64'): raise RuntimeError('Use validated x86_64 runtime')
    import unicorn
    from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_ESP,UC_X86_REG_EIP
    caster,target,caster2,friend = (0x5DA3A0+i*0x150 for i in range(4))
    sprites,images,pool,ai,stack,stop = 0x800000,0x801000,0x810000,0x812000,0x900000,0xA00000
    player = 7
    image=pe.get_memory_mapped_image();size=(pe.OPTIONAL_HEADER.SizeOfImage+4095)&~4095
    allow=list(WITNESSES.values())+[projectiles.BLOCKS['bullet_hit_dispatch']]+list(damageprobe.ALLOW)+list(imageinit.BLOCKS.values())+list(creation.BLOCKS.values())+list(families.WITNESSES.values())+list(reaction.WITNESSES.values())+list(requirements.WITNESSES.values())+list(primary.BLOCKS.values())+list(spatial.BLOCKS.values())
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


    def environment(u):
        put(u,0x543230,2048,2);put(u,0x543234,2048,2)
        put(u,0x544694,0);put(u,0x516d38,0);put(u,0x5c7a20,0);put(u,0x6d3140,0);put(u,0x673930,0)
        put(u,0x525eb2,dat['cooldown'][34],1);put(u,0x525cc4,dat['damage'][34],2)
        put(u,0x5259aa,dat['inner_splash'][33],2)
        put(u,0x59b210,0x827000);put(u,0x827008,0x100)
        for j in range(4):
            put(u,images+j*0x100+0xa,11,1);put(u,images+j*0x100+0xc,4,2)
        for owner in range(12):put(u,0x6726d0+owner*36,2,1)
        u.mem_write(0x51b740,bytes(dat['order_weapon']));u.mem_write(0x51b8c0,bytes(dat['order_animation']));u.mem_write(0x5258e0,bytes(dat['explosion']))
    for current,cost,cheat in((0,1,False),(100*256,100*256,False),(200*256,100*256,False),(255,256,False),(123,0x10001,False),(0,65535,True)):
        u=new();environment(u);put(u,caster+0xa2,current,2);put(u,0x5432fc,0x800 if cheat else 0);ex=call(u,0x435300,args=(cost,))
        record('complete energy debit truncates argument and wraps word subtraction unless Gathering cheat active',{'current_raw_energy':current,'raw_argument':cost,'cheat800':cheat},ex,get(u,caster+0xa2,2),current if cheat else(current-(cost&65535))&65535)
    for oldtimer,oldhp,burrow,properties in((0,0,False,0),(1,1,False,0),(167,200*256,False,0),(0,0,True,0),(0,0,False,0x2000000),(0,0,False,0x4000000)):
        u=new();environment(u);put(u,target+0x114,oldtimer,1);put(u,target+0x112,oldhp,2);put(u,target+0xdc,17 if burrow else 1);put(u,0x521660,properties)
        ex=call(u,0x438900,ecx=target)
        record('complete Matrix application resets capacity and duration with real overlay allocation failure',{'initial_timer':oldtimer,'initial_capacity':oldhp,'burrowed':burrow,'properties':properties,'image_pool_empty':True},ex,
               [get(u,target+0x112,2),get(u,target+0x114,1)],[250*256,168])
    for oldtimer,burrow,ownerarg in((0,False,7),(1,False,7),(255,False,7),(0,True,7),(0,False,0x107)):
        u=new();environment(u);put(u,target+0x118,oldtimer,1);put(u,target+0xdc,17 if burrow else 1)
        ex=call(u,0x438960,ecx=target,args=(caster,ownerarg))
        record('complete Irradiate application uses unchanged weapon cooldown and replaces source pointer and owner byte',{'initial_timer':oldtimer,'burrowed':burrow,'attacker':caster,'owner_argument':ownerarg,'stock_cooldown':dat['cooldown'][34],'image_pool_empty':True},ex,
               [get(u,target+0x118,1),get(u,target+0x11c),get(u,target+0x120,1)],[dat['cooldown'][34],caster,ownerarg&255])
    for timer in(0,1,75,255):
        u=new();environment(u);put(u,target+0x118,timer,1);put(u,target+0x11c,caster);put(u,target+0x120,7,1);ex=call(u,0x438620,ecx=target)
        record('complete Irradiate cleanup clears timer and attacker and sets owner sentinel8 after actual absent-overlay search',{'initial_timer':timer,'attacker':caster,'owner':7,'existing_overlay':False},ex,
               [get(u,target+0x118,1),get(u,target+0x11c),get(u,target+0x120,1)],[0,0,8])
    for stasis,selected,source_kind in((0,False,'caster'),(1,False,'caster'),(0,True,'caster'),(0,False,'target'),(0,False,'null'),(0,False,'caster_subunit')):
        u=new(target_owner=0);environment(u);put(u,target+0x119,stasis,1);put(u,target+0xa2,100*256,2);put(u,target+0x60,50*256)
        put(u,sprites+0x100+0xe,8 if selected else 0,1)
        if source_kind=='caster_subunit':put(u,caster+0x70,target)
        source=target if source_kind=='target' else 0 if source_kind=='null' else caster
        ex=call(u,0x433fc0,ecx=640,edx=512,args=(source,));affected=not stasis and source_kind not in('target','caster_subunit')
        record('complete EMP area effect exempts source and its subunit or stasis while clearing all other owners energy and shields',{'target_stasis':stasis,'selected':selected,'source_kind':source_kind,'target_owner':0,'stock_radius':dat['inner_splash'][33]},ex,
               [get(u,target+0xa2,2),get(u,target+0x60),get(u,images+0x100+0xc,2),get(u,0x5d6e48),get(u,0x5d6e5c)],
               [0 if affected else 100*256,0 if affected else 50*256,5 if affected and selected else 4,0,0])
    for initial_state,timer in((1,22),(1,1),(2,0),(0,0)):
        u=new(ident=71);environment(u);put(u,caster+0x4d,137,1);put(u,caster+0x4e,initial_state,1);put(u,caster+0x54,timer,1);put(u,caster+0x58,640,2);put(u,caster+0x5a,512,2)
        ex=call(u,0x42fc40);starts=initial_state!=1
        record('complete Recall start debits stock cost and sets state1 timer22 even when original lone-sprite pool exhausted',{'initial_state':initial_state,'initial_order_timer':timer,'lone_sprite_pool_empty':True,'sound_backend_disabled':True,'original_rng_disabled':True},ex,
               [get(u,caster+0xa2,2),get(u,caster+0x4e,1),get(u,caster+0x54,1)],[200*256-dat['energy'][21]*256 if starts else 200*256,1 if starts else initial_state,22 if starts else timer])
    for cheat in(False,True):
        u=new(ident=9);environment(u);put(u,caster+0x5c,target);put(u,caster+0x4d,141,1);put(u,caster+0x28,640,2);put(u,caster+0x2a,512,2);put(u,caster+0x20,4,1);put(u,0x5432fc,0x800 if cheat else 0)
        put(u,0x521ad8+9,10,1)
        ex=call(u,0x438560)
        record('complete Matrix order composes original target permission range stop debit effect and primary resume',{'cheat800':cheat,'target_distance':128,'target_visible':True,'sight_range_byte':10,'image_pool_empty':True,'sound_backend_disabled':True},ex,
               [get(u,caster+0xa2,2),get(u,target+0x112,2),get(u,target+0x114,1),get(u,caster+0x4d,1)],[200*256 if cheat else(200-dat['energy'][6])*256,250*256,168,156])
    for timer,capacity in((0,64000),(1,0),(1,1),(1,64000),(2,64000),(168,64000)):
        u=new();environment(u);put(u,target+0x114,timer,1);put(u,target+0x112,capacity,2);ex=call(u,0x4351e0,ecx=target)
        record('complete spell timer service decrements Matrix and invokes real depletion cleanup exactly at one-to-zero',{'Matrix_timer':timer,'Matrix_capacity':capacity,'all_other_effect_timers':0,'no_existing_overlays':True},ex,
               [get(u,target+0x114,1),get(u,target+0x112,2)],[max(timer-1,0),0 if timer==1 else capacity])
    for timer in(0,1,2,75):
        u=new(hp=100,properties=0x10000);environment(u);put(u,target+0x118,timer,1);put(u,target+0x11c,caster);put(u,target+0x120,player,1);put(u,target+0xdc,17)
        put(u,0x525748+34,4,1);put(u,0x521ca8,3,1)
        ex=call(u,0x4351e0,ecx=target);debit=dat['damage'][34]*256//dat['cooldown'][34]if timer else 0
        record('complete Irradiate timer service damages organic burrowed self on final tick before cleanup',{'Irradiate_timer':timer,'burrowed':True,'target_organic':True,'attacker_owner':player,'weapon_type4':True,'stock_damage':dat['damage'][34],'stock_cooldown':dat['cooldown'][34]},ex,
               [get(u,target+8),get(u,target+0x118,1),get(u,target+0x11c),get(u,target+0x120,1)],
               [100*256-debit,max(timer-1,0),0 if timer==1 else caster,8 if timer==1 else player])
    for weapon,target_kind in((33,'ordinary'),(34,'ordinary'),(34,'null'),(34,'dying')):
        u=new();environment(u);bullet=0x812300;put(u,bullet+0x60,weapon,1);put(u,bullet+0x4c,player,1);put(u,bullet+0x64,caster);put(u,bullet+0x5c,0 if target_kind=='null'else target);put(u,bullet+0xc,sprites+0x100)
        put(u,target+0xa2,123*256,2);put(u,target+0x60,45*256)
        if target_kind=='dying':put(u,target+0x4d,0,1);put(u,target+0x4e,1,1)
        ex=call(u,0x4090e0,ecx=bullet)
        record('complete original bullet hit dispatcher composes stock EMP or Irradiate effect acceptance',{'weapon':weapon,'stock_explosion_type':dat['explosion'][weapon],'target_kind':target_kind,'source':caster,'owner':player},ex,
               [get(u,target+0xa2,2),get(u,target+0x60),get(u,target+0x118,1),get(u,target+0x11c),get(u,target+0x120,1)],
               [0 if weapon==33 else 123*256,0 if weapon==33 else 45*256,dat['cooldown'][34]if weapon==34 and target_kind=='ordinary'else 0,caster if weapon==34 and target_kind=='ordinary'else 0,player if weapon==34 and target_kind=='ordinary'else 0])
    for order in(119,122,143):
        for signal,target_present in((0,True),(0,False),(2,True)):
            u=new();environment(u);put(u,caster+0x4d,order,1);put(u,caster+0x4e,2,1);put(u,caster+0x4f,signal,1);put(u,caster+0x5c,target if target_present else 0)
            ex=call(u,0x434460)
            record('complete generic spell order state2 follows target or consumes signal2 and resumes actual primary order',{'order':order,'state':2,'signal2':bool(signal),'target_present':target_present,'stock_weapon':dat['order_weapon'][order]},ex,
                   [get(u,caster+0x4d,1),get(u,caster+0x4f,1),get(u,caster+0x58),get(u,caster+0xa2,2)],
                   [156 if signal else order,0,0 if signal or not target_present else(640|(512<<16)),200*256])
    for energy,target_kind,pool_empty in((99*256+255,'ordinary',False),(100*256,'ordinary',False),(100*256+1,'ordinary',False),(200*256,'null',False),(200*256,'building',False),(200*256,'stasis',False),(200*256,'ordinary',True),(200*256,'existing_matrix',False)):
        u=new(ident=9);environment(u);put(u,caster+0x5c,0 if target_kind=='null'else target);put(u,caster+0x4d,141,1);put(u,caster+0x28,640,2);put(u,caster+0x2a,512,2);put(u,caster+0x20,4,1);put(u,0x521ad8+9,10,1);put(u,caster+0xa2,energy,2)
        if target_kind=='building':put(u,0x521660,1)
        if target_kind=='stasis':put(u,target+0x119,1,1)
        if target_kind=='existing_matrix':put(u,target+0x114,168,1);put(u,target+0x112,1,2)
        if pool_empty:put(u,0x5aef00,0);put(u,0x5aef04,0)
        ex=call(u,0x438560);accepted=energy>=dat['energy'][6]*256 and target_kind not in('null','building','stasis')
        record('complete Matrix order acceptance distinguishes exact energy permission reapplication and failed idle allocation',{'raw_energy':energy,'target_kind':target_kind,'order_pool_empty':pool_empty,'sight_range_byte':10,'image_pool_empty':True,'sound_backend_disabled':True},ex,
               [get(u,caster+0xa2,2),get(u,target+0x112,2),get(u,target+0x114,1),get(u,caster+0x4d,1)],
               [energy-dat['energy'][6]*256 if accepted else energy,64000 if accepted else 0,168 if accepted else 0,141 if pool_empty else 156])
    for timer in(0,1,22):
        u=new(ident=71);environment(u);put(u,caster+0x4d,137,1);put(u,caster+0x4e,1,1);put(u,caster+0x54,timer,1);put(u,caster+0x58,640,2);put(u,caster+0x5a,512,2)
        # Only the caster is indexed, outside the target64pixel area.
        spatial_indices(u,[caster]);ex=call(u,0x42fc40)
        record('complete Recall state1 waits on timer without decrement and resumes order after an empty actual spatial enumeration',{'state':1,'timer':timer,'area_center':[640,512],'indexed_units':[caster]},ex,
               [get(u,caster+0x4d,1),get(u,caster+0x54,1),get(u,caster+0xa2,2),get(u,0x665b68),get(u,0x5d6e48),get(u,0x5d6e5c)],
               [137 if timer else 156,timer,200*256,0,0,0])
    for subject_organic,neighbor_delta,neighbor_burrowed in((False,16,False),(True,16,False),(False,33,False),(False,34,False),(False,16,True)):
        u=new(target_id=0,hp=100,properties=0x10000 if subject_organic else 0);environment(u);unit(u,friend,15,player,640+neighbor_delta,512,hp=40,status=17 if neighbor_burrowed else 1,properties=0x10000)
        put(u,target+0x11c,caster);put(u,target+0x120,player,1);put(u,0x525748+34,4,1)
        for ident in(0,15):
            put(u,0x521ca8+ident,3,1);u.mem_write(0x522c38+ident*8,bytes(8))
        spatial_indices(u,[caster,target,friend]);ex=call(u,0x4385d0,ecx=target);debit=dat['damage'][34]*256//dat['cooldown'][34]
        record('complete Irradiate area search damages nearby organic units even when irradiated subject is nonorganic',{'subject_organic':subject_organic,'neighbor_center_delta':neighbor_delta,'bounding_extents':0,'neighbor_burrowed':neighbor_burrowed,'all_owners':player},ex,
               [get(u,target+8),get(u,friend+8),get(u,0x5d6e48),get(u,0x5d6e5c)],
               [100*256-debit if subject_organic else 100*256,40*256-debit if neighbor_delta<=33 and not neighbor_burrowed else 40*256,0,0])
    u=new(hp=100,properties=0x10000);environment(u);put(u,target+0xdc,0x41);put(u,target+0x118,1,1);put(u,target+0x11c,caster);put(u,target+0x120,player,1)
    ex=call(u,0x4351e0,ecx=target)
    record('complete final Irradiate tick in transport with null carrier applies no damage but still clears attribution',{'in_transport':True,'carrier_pointer':0,'timer':1,'organic':True},ex,
           [get(u,target+8),get(u,target+0x118,1),get(u,target+0x11c),get(u,target+0x120,1)],[100*256,0,0,8])
    return {'format_version':1,'original_code_executed':True,'complete_game_executed':False,'windows_or_host_api_called':False,'helper_stubs':[],'all_matched':True,
            'case_count':len(cases),'complete_return_count':len(cases),'prefix_case_count':0,'cases':cases,
            'allowlisted_preferred_va_ranges':[[hex(a),hex(b)]for a,b in allow],
            'runtime_versions':{'python':platform.python_version(),'unicorn':unicorn.__version__,'capstone':capstone.__version__,'pefile':pefile.__version__,'architecture':platform.machine(),'system':platform.system()}}

def main():
 p=argparse.ArgumentParser(description=__doc__)
 for name in('exe','orders-dat','weapons-dat','techdata-dat','out'):p.add_argument('--'+name,type=Path,required=True)
 p.add_argument('--execute-isolated-original-code',action='store_true');p.add_argument('--probe-out',type=Path);a=p.parse_args()
 if a.execute_isolated_original_code and not a.probe_out:p.error('Execution requires --probe-out')
 raw=a.exe.read_bytes();orders_raw=a.orders_dat.read_bytes();weapons=a.weapons_dat.read_bytes();tech=a.techdata_dat.read_bytes();assert hashlib.sha256(raw).hexdigest()==TARGET_HASH
 dat=dat_profile(orders_raw,weapons,tech);orders=primary.stock_orders(orders_raw);pe=pefile.PE(data=raw)
 assert pe.OPTIONAL_HEADER.ImageBase==0x400000 and pe.FILE_HEADER.Machine==0x14c
 md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);sha=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
 result={'target_sha256':TARGET_HASH,'target_version':'1.17.0.1','inspector_sha256':sha,'dependency_script_sha256':dependencies(),'original_code_executed':False,'witnesses':{},'stock_effect_inputs':{'Irradiate_cooldown':dat['cooldown'][34],'Irradiate_damage':dat['damage'][34],'EMP_inner_splash':dat['inner_splash'][33]}}
 result['main_order_dispatch_routes']={}
 for orderid in(119,122,137,141,143):
  index=pe.get_data(0x439d5c+orderid-1-0x400000,1)[0];wrapper=struct.unpack('<I',pe.get_data(0x439bcc+index*4-0x400000,4))[0]
  instructions=list(md.disasm(pe.get_data(wrapper-0x400000,8),wrapper))
  result['main_order_dispatch_routes'][str(orderid)]={'mapping_preferred_va':hex(0x439d5c+orderid-1),'index':index,'wrapper_preferred_va':hex(wrapper),'wrapper_instructions':[{'address':hex(i.address),'bytes':i.bytes.hex(),'mnemonic':i.mnemonic,'operands':i.op_str}for i in instructions]}
 for name,(lo,hi)in WITNESSES.items():
  code=pe.get_data(lo-0x400000,hi-lo);ins=list(md.disasm(code,lo));assert sum(i.size for i in ins)==len(code),name
  result['witnesses'][name]={'start_preferred_va':hex(lo),'end_exclusive_preferred_va':hex(hi),'bytes_sha256':hashlib.sha256(code).hexdigest(),'instructions':[{'address':hex(i.address),'bytes':i.bytes.hex(),'mnemonic':i.mnemonic,'operands':i.op_str}for i in ins]}
 result['stock_dat_hashes']={'orders':primary.ORDERS_HASH,'weapons':families.WEAPONS_HASH,'techdata':families.TECH_HASH}
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n')
 if a.execute_isolated_original_code:
  run=execute(pe,orders,dat);run.update({'target_sha256':TARGET_HASH,'target_version':'1.17.0.1','probe_script_sha256':sha,'dependency_script_sha256':dependencies(),'stock_dat_hashes':result['stock_dat_hashes']})
  a.probe_out.parent.mkdir(parents=True,exist_ok=True);a.probe_out.write_text(json.dumps(run,indent=2)+'\n');print(json.dumps({'cases':run['case_count'],'complete':run['complete_return_count'],'sha256':sha}))
 else:print('Wrote static spell effects evidence; original code not executed')
if __name__=='__main__':main()
