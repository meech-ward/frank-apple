#!/usr/bin/env node
// Controller safety checks with a simulated badge. Live browser/device checks are separate.
import { readFileSync } from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
const html=readFileSync(new URL('../src/web_control.html',import.meta.url),'utf8');
const source=html.split('<script>')[1].split('</script>')[0].replace('renderCards();refresh();','renderCards();');
function fixture(){
 const els=new Map(), sent=[];let clock=0,postHook=null,apiHook=null;
 const element=()=>({hidden:false,disabled:false,textContent:'',innerHTML:'',value:'',dataset:{},classList:{toggle(){},remove(){}},setAttribute(){},scrollIntoView(){},replaceChildren(){},append(){}});
 const get=id=>{if(!els.has(id))els.set(id,element());return els.get(id);};
 const state={menu:false,queued:0,graphics:false,basic_prompt:true,screen:']\n'};
 const context=vm.createContext({document:{getElementById:get,createElement:element,querySelectorAll:()=>[]},Blob,localStorage:{getItem:()=>null,setItem(){}},AbortController,Date:{now:()=>clock+=1000},setTimeout:(f,ms)=>setTimeout(f,Math.min(ms,1)),clearTimeout,
 fetch:async(path,opts)=>{if(apiHook){const result=await apiHook(path,opts);if(result)return result;}if(path.startsWith('/disks')&&opts.method==='GET')return {ok:true,json:async()=>({files:[],next:0,free:1048576})};if(opts.method==='POST'){sent.push([path,opts.body]);if(postHook)await postHook(path,opts.body);else if(path==='/type')state.screen=']'+opts.body+'\n';return {ok:true};}return {ok:true,json:async()=>({...state})};}});
 vm.runInContext(source,context);
 return {run:code=>vm.runInContext(code,context),get,sent,state,hook:fn=>postHook=fn,api:fn=>apiHook=fn};
}
{
 const f=fixture();f.run('startLesson(lessons[1]);stepIndex=1;renderStep();');
 f.get('stepAction').onclick();assert.equal(f.sent.length,0,'Replacing RAM must wait for inline confirmation');
 f.get('cancelAction').onclick();assert.equal(f.sent.length,0,'Cancel must not type');
 f.get('stepAction').onclick();await f.get('confirmAction').onclick();
 // Inline handler dispatches asynchronous work; await the exposed busy flag via the fixture.
 while(f.run('busy'))await new Promise(r=>setTimeout(r,2));
 assert(f.sent.some(([,b])=>b==='NEW\r'));
 assert(f.sent.some(([,b])=>b==='80 PRINT "YOU GOT IT!"\r'));
 assert(!f.sent.some(([p,b])=>p==='/key'&&b==='break'),'Idle BASIC must not receive Ctrl-C before a command');
 assert.equal(f.run('stepComplete'),true);
}
{
 const f=fixture();f.run('startLesson(lessons[0]);stepIndex=3;renderStep();');
 await f.run('doStep()');assert.equal(f.run('stepComplete'),false,'Wrong output must not count as completion');
 assert.equal(f.sent.length,0,'Check is read-only');
 f.state.screen='42\n]\n';f.run('paint('+JSON.stringify(f.state)+')');assert.equal(f.run('stepComplete'),false,'Screen updates must not auto-advance');
 await f.run('doStep()');assert.equal(f.run('stepComplete'),true);
 assert.equal(f.run('stepIndex'),3,'Successful check still waits for Next');
}
{
 const f=fixture();f.run('startLesson(lessons[1]);stepIndex=1;renderStep();');
 f.hook(async(p,b)=>{f.state.screen=']'+b+'\n';if(p==='/type'&&b.startsWith('20 '))await f.run('stop()');});
 await f.run('doStep()');assert(!f.sent.some(([,b])=>b.startsWith('30 ')),'Stop must cancel remaining program lines');assert.equal(f.run('stepComplete'),false);
}
{
 const f=fixture();f.run('startLesson(lessons[1]);stepIndex=7;renderStep();');
 f.hook((p,b)=>{if(b.startsWith('SAVE'))f.state.screen=']SAVE GUIDE GAME\nDISK FULL\n]\n';});
 await f.run('doStep()');assert.equal(f.run('stepComplete'),false,'A disk error must not count as a save');
 assert(f.get('stepResult').textContent.includes('error'));
}
{
 const f=fixture();f.state.screen='WHAT IS YOUR NAME? SAM\nYOU GOT IT!\n]\n';
 f.run('startLesson(lessons[1]);stepIndex=3;renderStep();');await f.run('doStep()');assert.equal(f.sent.length,0,'Old question text at a BASIC prompt must not accept an app reply');
}
{
 const f=fixture();f.state.screen=']SAVE GUIDE GAME\nFILE LOCKED\n]\n';
 f.run('startLesson(lessons[1]);stepIndex=7;renderStep();');
 f.hook(()=>{});await f.run('doStep()');assert.equal(f.run('stepComplete'),false,'A repeated FILE LOCKED error must not count as a save');
}
{
 const f=fixture();f.run('startLesson(lessons[1]);');
 assert.equal(f.get('next').disabled,false,'Next must work before doing a step, even offline');
 f.get('next').onclick();assert.equal(f.run('stepIndex'),1);
 f.get('previous').onclick();assert.equal(f.run('stepIndex'),0);
 f.get('next').onclick();assert.equal(f.run('stepIndex'),1,'Reviewing must not force a replay');
 f.get('stepSelect').value='8';f.get('stepSelect').onchange();assert.equal(f.run('stepIndex'),8);
 assert.equal(f.sent.length,0,'Back/Next/jump must never send badge commands');
 f.run('stepChecks[active.id]=new Set([8]);');f.get('previous').onclick();f.get('next').onclick();
 assert.equal(f.run('stepComplete'),true,'Returning to a checked step must preserve completion');
 assert(f.get('stepResult').textContent.includes('Previously completed'));
 f.get('next').onclick();f.get('next').onclick();
 assert.equal(f.get('finishLabel').textContent,'End of walkthrough');
 assert.equal(f.run('completed.game'),undefined,'Skipping steps must not claim that they ran');
}
{
 const f=fixture();f.get('exactCase').checked=true;f.get('command').value='Mixed Case';
 await f.get('sendRaw').onclick();assert.deepEqual(f.sent,[['/text','Mixed Case']],'Application typing preserves case and does not add Return');
 f.sent.length=0;f.get('documentText').value='Club note\nBring a disk.';await f.get('documentSend').onclick();
 assert.deepEqual(f.sent,[['/text','Club note\rBring a disk.']]);
 f.sent.length=0;f.get('appleShortcut').value='S';await f.get('appleSend').onclick();assert.deepEqual(f.sent,[['/apple','S']]);
 f.run('paint({...current,columns:80,screen:"AppleWorks"})');assert(f.get('mode').textContent.includes('80-column'));
}
{
 const f=fixture();f.run("startLesson(lessons.find(l=>l.id==='visicalc'))");f.get('stepAction').onclick();
 assert.equal(f.sent.length,0,'Boot waits for inline confirmation');f.get('cancelAction').onclick();assert.equal(f.sent.length,0);
 f.state.basic_prompt=false;f.state.screen='       A        B';
 f.run("stepIndex=2;renderStep()");f.hook(async(p)=>{if(p==='/type')await f.run('stop()');});
 await f.run('doStep()');assert.equal(f.sent.filter(([p])=>p==='/type').length,1,'Stop cancels the rest of an application macro');
 assert.equal(f.run('stepComplete'),false);
}
{
 const f=fixture();f.state.basic_prompt=false;
 f.state.screen='File: CLUB.NOTE  REVIEW/ADD/CHANGE\n';
 assert.equal(f.run('awEdit('+JSON.stringify(f.state)+')'),false,'AppleWorks heading appears before its editor is ready');
 f.state.screen+='Type entry or use A commands';
 assert.equal(f.run('awEdit('+JSON.stringify(f.state)+')'),true);
}
console.log('PASS: confirmation/cancel, paced program entry, idle-prompt recovery, explicit result checking, Stop cancellation, disk errors, stale app prompts, free navigation, and preserved step completion.');
{
 const f=fixture();f.get('httpStarter').onclick();
 assert.equal(f.sent.length,0,'Selecting the HTTP starter only edits the textarea');
 assert.equal(f.get('listing').value,readFileSync(new URL('../basic/http-demo.bas',import.meta.url),'utf8').trim());
 await f.run('sendLine(\'20 u$="https://httpbin.org/get?message=Hello"\',epoch,true)');
 assert.deepEqual(f.sent,[['/text','20 U$="https://httpbin.org/get?message=Hello"\r']]);
 f.sent.length=0;f.get('command').value='20 u$="https://httpbin.org/uuid"';
 await f.get('send').onclick();assert.deepEqual(f.sent,[['/text','20 U$="https://httpbin.org/uuid"\r']]);
}
console.log('PASS: HTTP starter and case-sensitive BASIC URLs.');

{
 const f=fixture();f.get('httpApiStarter').onclick();
 assert.equal(f.sent.length,0,'Selecting the API starter only edits the textarea');
 assert.equal(f.get('listing').value,readFileSync(new URL('../basic/http-api-demo.bas',import.meta.url),'utf8').trim());
}
console.log('PASS: HTTP API starter matches the runnable BASIC source.');

{
 const f=fixture(),calls=[];let received=0;
 f.api(async(path,opts)=>{
  if(!path.startsWith('/upload/'))return;
  calls.push([path,opts.body]);
  if(path==='/upload/start')return {ok:true,json:async()=>({id:9,received:0,size:143360})};
  if(path==='/upload/chunk'){
   const p=Buffer.from(opts.body);assert.equal(p.readUInt32LE(0),9);assert.equal(p.readUInt32LE(4),received);assert(p.length<=1032);
   received+=p.length-8;return {ok:true,json:async()=>({id:9,received})};
  }
  return {ok:true,json:async()=>({name:'Custom.dsk'})};
 });
 await f.run("uploadSelected([{file:Object.assign(new Blob([new Uint8Array(143360)]),{name:'Custom.dsk'}),name:'Custom.dsk'}])");
 assert.equal(received,143360);assert.equal(calls.at(-1)[0],'/upload/finish');
 assert(!calls.some(([p])=>p==='/disks/mount'),'Uploading never boots or inserts automatically');
 assert(f.get('libraryNotice').textContent.startsWith('Added 1 disk'));assert.equal(f.run('busy'),false);
 assert.equal(f.run("suggestDiskName('AppleWorks 2.0 Disk 1 Boot - Apple 1986.po')"),'AppleWorks Startup.po');
 assert.throws(()=>f.run("checkDiskFile({size:143360,name:'x.dsk'},'x.po')"),/sector order/);
 assert.throws(()=>f.run("checkDiskFile({size:819200},'large.po')"),/standard 140 KB/);
 assert.throws(()=>f.run("checkDiskFile({size:143360},'../wifi.ini')"),/filename/);
}
{
 const f=fixture(),calls=[];let chunks=0;
 f.api(async(path,opts)=>{
  if(!path.startsWith('/upload/'))return;
  calls.push(path);
  if(path==='/upload/start')return {ok:true,json:async()=>({id:3})};
  if(path==='/upload/chunk'){if(++chunks===2)f.get('cancelUpload').onclick();return {ok:true,json:async()=>({received:chunks*1024})};}
  return {ok:true,json:async()=>({})};
 });
 await f.run("uploadSelected([{file:new Blob([new Uint8Array(143360)]),name:'Cancel.dsk'}])");
 assert.equal(chunks,2);assert(calls.includes('/upload/cancel'));assert(!calls.includes('/upload/finish'));
 assert(f.get('libraryNotice').textContent.includes('cancelled'));
}
{
 const f=fixture();let posts=0;
 f.api(async(path)=>{if(path==='/upload/start'){posts++;return {ok:false,text:async()=> 'That name already exists. Choose a new name.'};}});
 await f.run("uploadSelected([{file:new Blob([new Uint8Array(143360)]),name:'Keep.dsk'}])");
 assert.equal(posts,1);assert(f.get('libraryNotice').textContent.includes('already exists'));
 f.get('basicFile').files=[{name:'Mine.bas',size:30,text:async()=> 'NEW\n10 PRINT "MINE"\nRUN\n'}];
 await f.get('basicFile').onchange();assert.equal(f.get('listing').value,'10 PRINT "MINE"');
 assert.equal(f.sent.length,0,'Opening a source file only edits the browser');
}
{
 const f=fixture(),calls=[];
 f.api(async(path,opts)=>{
  if(path==='/disks/mount'){calls.push(opts.body);return {ok:true,json:async()=>({id:8,pending:true})};}
  if(path==='/mount')return {ok:true,json:async()=>({id:8,pending:false,ok:true})};
 });
 f.run("bootChoice='My App.dsk'");assert.equal(calls.length,0);
 f.get('bootNo').onclick();assert.equal(calls.length,0);
 f.run("bootChoice='My App.dsk'");f.get('bootYes').onclick();
 while(f.run('busy'))await new Promise(r=>setTimeout(r,2));
 assert.deepEqual(calls,['boot\n0\nMy App.dsk']);
 await f.run("operation(token=>chooseDisk('Data.po',token,{drive:2,boot:false}))");
 assert.equal(calls.at(-1),'insert\n1\nData.po');
}
console.log('PASS: disk upload framing/progress, no automatic boot, cancellation, conflicts, filename/format validation, BASIC file review, and explicit Boot/Insert.');
