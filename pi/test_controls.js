// Execute the actual page controller with fake DOM/network; never drives hardware.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const source = fs.readFileSync(__dirname + '/server.py', 'utf8');
const js = source.split('<script>')[1].split('</script>')[0];

function setup(){
  const events = {}, elements = {}, requests = [], intervals = [];
  let fail = false, deferred = null, hold = false, tick = 0;
  const ctx = new Proxy({}, {get:()=>()=>{}});
  function element(id){
    return elements[id] ||= {
      width:320, height:320, clientWidth:320, clientHeight:320, value:'50',
      style:{}, textContent:'', dataset:{code:id}, captured:new Set(),
      classList:{toggle(){}}, closest:()=>false,
      getContext:()=>ctx, getBoundingClientRect:()=>({left:0,top:0,width:320,height:320}),
      addEventListener:(name, fn)=>{events[id+':'+name] = fn;},
      hasPointerCapture(p){return this.captured.has(p);},
      setPointerCapture(p){this.captured.add(p);},
      releasePointerCapture(p){this.captured.delete(p);}
    };
  }
  const document = {
    hidden:false, getElementById:element,
    querySelectorAll:()=>['KeyW','KeyA','KeyS','KeyD'].map(element),
    addEventListener:(name, fn)=>{events['document:'+name] = fn;}
  };
  const sandbox = {document, console, AbortController,
    performance:{now:()=>tick+=100},
    window:{addEventListener:(name, fn)=>{events['window:'+name] = fn;}},
    setTimeout:()=>1, clearTimeout:()=>{}, setInterval:fn=>intervals.push(fn),
    fetch(url){
      if(url === '/scan') return Promise.resolve({json:async()=>({points:[],online:false})});
      const query = new URL(url, 'http://test').searchParams;
      requests.push({x:Number(query.get('x')), y:Number(query.get('y'))});
      if(fail) return Promise.reject(new Error('offline'));
      if(hold){hold=false;return new Promise(resolve=>{deferred=resolve;});}
      return Promise.resolve({ok:true,json:async()=>({ok:true})});
    }
  };
  vm.runInNewContext(js, sandbox);
  return {requests, elements, document, intervals,
    async event(name, data={}){
      events[name]({code:'',button:0,pointerId:1,target:element('body'),preventDefault(){},...data});
      await new Promise(setImmediate);
    },
    last:()=>requests.at(-1), fail:()=>{fail=true;}, hold:()=>{hold=true;},
    async resolve(){deferred({ok:true,json:async()=>({ok:true})});await new Promise(setImmediate);}
  };
}

(async()=>{
  let p = setup();
  for(const [code, expected] of [
    ['KeyW',{x:0,y:50}], ['KeyS',{x:0,y:-50}],
    ['KeyA',{x:-50,y:0}], ['KeyD',{x:50,y:0}]
  ]){
    await p.event('window:keydown',{code}); assert.deepEqual(p.last(),expected);
    await p.event('window:keyup',{code}); assert.deepEqual(p.last(),{x:0,y:0});
  }
  await p.event('window:keydown',{code:'KeyW'});
  await p.event('window:keydown',{code:'KeyA'});
  assert.deepEqual(p.last(),{x:-35,y:35});
  await p.event('window:keyup',{code:'KeyW'});
  assert.deepEqual(p.last(),{x:-50,y:0});
  await p.event('window:keydown',{code:'KeyD'});
  assert.deepEqual(p.last(),{x:0,y:0});
  await p.event('window:blur');
  await p.event('window:keydown',{code:'KeyW'});
  p.intervals[0](); await new Promise(setImmediate);
  assert.deepEqual(p.last(),{x:0,y:50});
  await p.event('window:keydown',{code:'Space'});
  assert.deepEqual(p.last(),{x:0,y:0});
  const count = p.requests.length;
  await p.event('window:keydown',{code:'KeyW',repeat:true});
  assert.equal(p.requests.length,count);
  await p.event('window:keydown',{code:'KeyW',ctrlKey:true});
  await p.event('window:keydown',{code:'KeyW',target:{closest:()=>true}});
  assert.equal(p.requests.length,count);
  await p.event('pad:pointerdown',{clientX:320,clientY:160});
  assert.deepEqual(p.last(),{x:100,y:0});
  p.intervals[0](); await new Promise(setImmediate);
  assert.deepEqual(p.last(),{x:100,y:0});
  await p.event('pad:pointercancel');
  assert.deepEqual(p.last(),{x:0,y:0});
  await p.event('window:keydown',{code:'KeyW'});
  p.document.hidden=true; await p.event('document:visibilitychange');
  assert.deepEqual(p.last(),{x:0,y:0});

  p=setup(); p.hold();
  await p.event('window:keydown',{code:'KeyW'});
  p.document.hidden=true; await p.event('window:blur');
  assert.equal(p.requests.length,1);
  await p.resolve(); assert.deepEqual(p.last(),{x:0,y:0});

  p=setup(); p.fail();
  await p.event('window:keydown',{code:'KeyW'});
  assert.deepEqual(p.last(),{x:0,y:0});
  assert.equal(p.requests.length,2); // failed movement + one zero, no retry loop
  console.log('WASD: mapping, combinations, hold, release, blur, pointer cancel and network failure OK');
})().catch(err=>{console.error(err);process.exitCode=1;});
