const assert = require('node:assert/strict');
const { test } = require('node:test');
const fs = require('node:fs');
const vm = require('node:vm');

async function page(hostname='192.168.4.1', apiHandler=null) {
  const html=fs.readFileSync('setup/index.html','utf8');
  function element(){return {value:'',hidden:false,disabled:false,textContent:'',children:[],required:false,
    replaceChildren(){this.children=[];this.value='';},append(child){this.children.push(child);},reportValidity(){return true;}};}
  const elements=Object.fromEntries([...html.matchAll(/id="([^"]+)"/g)].map(m=>[m[1],element()]));
  const calls=[];
  const context=vm.createContext({document:{getElementById:id=>elements[id],createElement:()=>element()},location:{hostname},navigator:{},
    TextEncoder,TextDecoder,setTimeout,clearTimeout,AbortSignal,JSON,
    fetch:async(url,options={})=>{
      const body=options.body?JSON.parse(options.body):null;calls.push({url,body,options});
      if(url==='/api/bootstrap')return {ok:true,json:async()=>({helper:!!apiHandler,csrf:'nonce',accounts:[]})};
      if(!apiHandler)throw Error('unexpected fetch: '+url);
      const data=await apiHandler(url,body);return {ok:!data.error,json:async()=>data};
    }});
  vm.runInContext(html.match(/<script>([\s\S]*?)<\/script>/)[1],context);
  await vm.runInContext('setupReady',context);
  for(const [key,value]of Object.entries({ssid:' wifi ',pwd:' pass ',serial:'01P00A123456789',region:'Global',username:' u_123 ',token:' token ',model:'P1S',host:'192.168.1.50',accessCode:'12345678'}))elements[key].value=value;
  return {elements,context,calls};
}
function mode(p,value){p.elements.mode.value=value;p.elements.mode.onchange();}
const prevent={preventDefault(){}};

test('AP manual form preserves WiFi whitespace and clears secrets after save',async()=>{
  const p=await page();let sent;
  assert.equal(p.elements.usb.hidden,true);assert.equal(p.elements.cloudChoice.disabled,true);
  p.context.fetch=async(url,options)=>{sent={url,config:JSON.parse(options.body)};return {ok:true,json:async()=>({status:'ok'})};};
  await p.elements.setup.onsubmit(prevent);
  assert.equal(sent.url,'/provision');assert.equal(sent.config.ssid,' wifi ');assert.equal(sent.config.pwd,' pass ');
  assert.equal(sent.config.mqtt.printerModel,'P1S');assert.equal(sent.config.mqtt.authToken,'token');assert.equal(p.elements.token.value,'');
});

test('LAN configuration needs neither account login nor Cloud token',async()=>{
  const p=await page();mode(p,'local');p.elements.token.value='';p.elements.username.value='';
  const config=await vm.runInContext('buildConfig()',p.context);
  assert.equal(config.mqtt.mode,'local');assert.equal(config.mqtt.host,'192.168.1.50');assert.equal(config.mqtt.accessCode,'12345678');
  assert.equal(config.mqtt.authToken,undefined);assert.equal(p.elements.token.required,false);assert.equal(p.elements.host.required,true);
});

test('device rejection preserves fields for correction',async()=>{
  const p=await page();p.context.fetch=async()=>({ok:false,json:async()=>({error:'invalid printer serial'})});
  await p.elements.setup.onsubmit(prevent);assert.equal(p.elements.status.textContent,'invalid printer serial');assert.equal(p.elements.token.value,' token ');
});

const printer={serial:'01P00A123456789',name:'My P1S',model:'P1S',supported:true,online:true};
function cloudApi(stage='authenticated'){
  return async(url,body)=>{
    if(url==='/api/login')return {session_id:'session',stage,email:'test@example.com'};
    if(url==='/api/verify')return {stage:'authenticated'};
    if(url==='/api/resend')return {stage:'email_code'};
    if(url==='/api/logout')return {};
    if(url==='/api/devices')return {email:'test@example.com',devices:[printer]};
    if(url==='/api/config')return {mqtt:{mode:'cloud',region:'Global',username:'u_999',authToken:'session-token',serialNumber:printer.serial,printerModel:'P1S'}};
    throw Error('unexpected endpoint');
  };
}
async function login(p){p.elements.email.value='test@example.com';p.elements.accountPassword.value='test-password';await p.elements.login.onclick();}

test('email login chooses a printer and builds configuration without manual credentials',async()=>{
  const p=await page('127.0.0.1',cloudApi());
  assert.equal(p.elements.mode.value,'cloud');await login(p);
  assert.equal(p.elements.printer.value,printer.serial);assert.equal(p.elements.accountPassword.value,'');assert.equal(p.elements.challenge.hidden,true);
  p.elements.token.value='';p.elements.username.value='';
  const config=await vm.runInContext('buildConfig()',p.context);
  assert.equal(config.mqtt.authToken,'session-token');assert.equal(config.mqtt.serialNumber,printer.serial);
  assert.equal(config.accountPassword,undefined);assert.equal(p.calls.find(c=>c.url==='/api/login').options.headers['X-XTouch-Setup'],'nonce');
});

test('email and authenticator challenges advance to the printer list',async()=>{
  for(const stage of ['email_code','two_factor']){
    const p=await page('127.0.0.1',cloudApi(stage));await login(p);
    assert.equal(p.elements.challenge.hidden,false);assert.equal(p.elements.save.disabled,true);
    p.elements.verificationCode.value='123456';await p.elements.verify.onclick();
    assert.equal(p.elements.printer.value,printer.serial);assert.equal(p.elements.verificationCode.value,'');assert.equal(p.elements.save.disabled,false);
  }
});

test('invalid code keeps the challenge retryable',async()=>{
  const handler=cloudApi('email_code');const p=await page('127.0.0.1',async(url,body)=>url==='/api/verify'?{error:'code_incorrect'}:handler(url,body));
  await login(p);p.elements.verificationCode.value='000000';await p.elements.verify.onclick();
  assert.equal(p.elements.challenge.hidden,false);assert.equal(p.elements.verify.disabled,false);assert.equal(p.elements.save.disabled,true);
  assert.notEqual(p.elements.cloudStatus.textContent,'code_incorrect');
});

test('expired sessions return to login instead of trapping the user at verification',async()=>{
  const handler=cloudApi('email_code');const p=await page('127.0.0.1',async(url,body)=>url==='/api/verify'?{error:'session_expired'}:handler(url,body));
  await login(p);p.elements.verificationCode.value='123456';await p.elements.verify.onclick();
  assert.equal(p.elements.challenge.hidden,true);assert.equal(p.elements.loginFields.hidden,false);assert.equal(p.elements.save.disabled,true);
  assert.equal(p.elements.restartLogin.hidden,true);
});

test('restarting login discards the previous session and permits account correction',async()=>{
  const p=await page('127.0.0.1',cloudApi('email_code'));await login(p);await p.elements.restartLogin.onclick();
  assert.equal(p.calls.find(c=>c.url==='/api/logout').body.session_id,'session');
  assert.equal(p.elements.loginFields.hidden,false);assert.equal(p.elements.challenge.hidden,true);assert.equal(p.elements.save.disabled,true);
});

test('existing account selection loads printers without requesting a password',async()=>{
  const p=await page('127.0.0.1',cloudApi());p.elements.account.value='existing';await p.elements.account.onchange();
  assert.equal(p.elements.printer.value,printer.serial);assert.equal(p.calls.some(c=>c.url==='/api/login'),false);
});

test('USB transport handles fragmented firmware replies without sending credentials over HTTP',async()=>{
  const p=await page('127.0.0.1');const sent=[];let controller;
  const serialPort={open:async()=>{},close:async()=>{},setSignals:async()=>{},
    readable:new ReadableStream({start(c){controller=c;}}),
    writable:new WritableStream({write(bytes){const command=new TextDecoder().decode(bytes);sent.push(command);
      const result=command.trim()==='XTOUCH STATUS'?{status:'ok',version:'0.9.213-cyd.2'}:{status:'ok'};
      const data=new TextEncoder().encode('[XTouch][SETUP] ready\nXTOUCH_RESULT '+JSON.stringify(result)+'\r\n');
      for(let i=0;i<data.length;i+=3)controller.enqueue(data.slice(i,i+3));}})};
  p.context.navigator.serial={requestPort:async()=>serialPort};p.context.fetch=()=>assert.fail('manual USB must not use HTTP');
  await p.elements.connect.onclick();await p.elements.setup.onsubmit(prevent);
  assert.equal(sent[0],'\nXTOUCH STATUS\n');const config=JSON.parse(sent[1].slice(17));assert.equal(config.pwd,' pass ');assert.equal(config.mqtt.username,'u_123');controller.close();
});

test('USB save without a board stops before requesting an account token',async()=>{
  const p=await page('127.0.0.1',cloudApi());await login(p);await p.elements.setup.onsubmit(prevent);
  assert.equal(p.calls.some(c=>c.url==='/api/config'),false);assert.match(p.elements.status.textContent,/USB/);
});

test('occupied serial port shows an inline retryable error without closing another connection',async()=>{
  const p=await page('127.0.0.1',cloudApi());await login(p);let closed=false;
  p.context.navigator.serial={requestPort:async()=>({open:async()=>{throw new DOMException('Failed to open serial port.','NetworkError');},close:async()=>{closed=true;}})};
  await p.elements.setup.onsubmit(prevent);
  assert.equal(closed,false);assert.equal(p.elements.connect.disabled,false);
  assert.match(p.elements.usbStatus.textContent,/이전 XTouch/);
  assert.equal(p.calls.some(c=>c.url==='/api/config'),false);
});

test('failure after opening a port releases it for retry',async()=>{
  const p=await page('127.0.0.1');let opened=false,closed=false;
  p.context.navigator.serial={requestPort:async()=>({open:async()=>{opened=true;},setSignals:async()=>{throw Error('signal failure');},close:async()=>{closed=true;}})};
  await p.elements.connect.onclick();
  assert.equal(opened,true);assert.equal(closed,true);assert.equal(p.elements.connect.disabled,false);
});

test('save connects first and ignores stale serial replies before exporting credentials',async()=>{
  const p=await page('127.0.0.1',cloudApi());await login(p);
  let controller,acknowledged=false;const sent=[];
  const send=value=>controller.enqueue(new TextEncoder().encode('XTOUCH_RESULT '+JSON.stringify(value)+'\n'));
  const serialPort={open:async()=>{},close:async()=>{},setSignals:async()=>{},
    readable:new ReadableStream({start(c){controller=c;}}),
    writable:new WritableStream({write(bytes){const command=new TextDecoder().decode(bytes);sent.push(command);
      if(command.trim()==='XTOUCH STATUS'){
        assert.equal(p.calls.some(c=>c.url==='/api/config'),false);
        send({status:'error',error:'unknown command'});send({status:'ok',version:'0.9.213-cyd.2'});
      }else{
        send({status:'ok',version:'0.9.213-cyd.2'});
        setTimeout(()=>{acknowledged=true;send({status:'ok'});},15);
      }
    }})};
  p.context.navigator.serial={requestPort:async()=>serialPort};
  await p.elements.setup.onsubmit(prevent);
  assert.equal(acknowledged,true);assert.equal(sent.length,2);
  assert.equal(JSON.parse(sent[1].slice(17)).mqtt.authToken,'session-token');
  assert.match(p.elements.usbStatus.textContent,/USB 연결됨/);
  await p.elements.disconnect.onclick();
  assert.equal(p.elements.connect.disabled,false);assert.equal(p.elements.disconnect.hidden,true);
});

test('USB handshake survives ESP32 reset and commands lost during cold boot',async()=>{
  const p=await page('127.0.0.1');let controller,requests=0;
  const serialPort={open:async()=>{},close:async()=>{},setSignals:async()=>{},
    readable:new ReadableStream({start(c){controller=c;}}),
    writable:new WritableStream({write(bytes){
      assert.equal(new TextDecoder().decode(bytes).trim(),'XTOUCH STATUS');requests++;
      if(requests===1)controller.enqueue(new TextEncoder().encode('ets Jul 29 2019\r\n[XTouch] CYD Cloud revision 0.9.213-cyd.2\n'));
      // The real board drops early requests while initialization is running.
      if(requests===4)controller.enqueue(new TextEncoder().encode('XTOUCH_RESULT {"status":"ok","version":"0.9.213-cyd.2"}\r\n'));
    }})};
  p.context.navigator.serial={requestPort:async()=>serialPort};
  await p.elements.connect.onclick();
  assert.match(p.elements.usbStatus.textContent,/USB 연결됨/);
  assert.equal(requests,4);assert.equal(p.elements.disconnect.hidden,false);
  await p.elements.disconnect.onclick();
});
