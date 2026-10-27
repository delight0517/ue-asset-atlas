import { frameRateFromSamples, nextAdaptiveDpr } from "/runtime/frame-rate.js";

export const contractVersion = 1;
export const moduleId = "asset-preview";

export function init(ctx, restored = {}) {
  const listeners = new AbortController();
  const on = (target, type, handler, options = {}) => target.addEventListener(type, handler, { ...options, signal: listeners.signal });
  const canvas = document.getElementById("asset-scene");
  const reset = document.getElementById("reset-view");
  const qualitySelect = document.getElementById("quality-mode");
  const stats = document.getElementById("render-stats");
  const objectSelect = document.getElementById("object-select");
  const selectedLabel = document.getElementById("selected-object-label");
  const resetObject = document.getElementById("reset-object");
  const objectControls = {
    x: document.getElementById("object-x"), y: document.getElementById("object-y"),
    rotation: document.getElementById("object-rotation"), scale: document.getElementById("object-scale")
  };
  if (!canvas) return { snapshot: () => restored, dispose: () => listeners.abort(), canReload: () => !document.hidden };

  const qualityCaps = { performance: .75, balanced: 1, quality: 1.5 };
  let qualityMode = restored.qualityMode || ctx.preferences.get("asset-atlas-preview-quality", "auto");
  if (!Object.hasOwn(qualityCaps, qualityMode) && qualityMode !== "auto") qualityMode = "auto";
  if (qualitySelect) qualitySelect.value = qualityMode;
  const deviceDpr = Math.min(window.devicePixelRatio || 1, 2);
  const memory = navigator.deviceMemory || 4;
  const cores = navigator.hardwareConcurrency || 4;
  let adaptiveDpr = deviceDpr * (memory <= 4 || cores <= 4 ? .75 : 1);
  const antialias = true;
  const gl = canvas.getContext("webgl", { alpha: true, antialias, powerPreference: "low-power", depth: true, stencil: false, preserveDrawingBuffer: false });
  if (!gl) {
    const fallback = canvas.getContext("2d");
    if (fallback) { fallback.fillStyle = "#91dfcf"; fallback.font = "14px system-ui"; fallback.fillText("WebGL is unavailable in this browser", 24, 38); }
    return { snapshot: () => restored, dispose: () => listeners.abort(), canReload: () => !document.hidden };
  }

  const vertexSource = `
    attribute vec3 aPosition;
    attribute vec3 aNormal;
    attribute vec3 aColor;
    uniform mat4 uViewProjection;
    uniform mat4 uModel;
    uniform float uSelected;
    varying vec3 vNormal;
    varying vec3 vColor;
    void main() {
      vec4 world = uModel * vec4(aPosition, 1.0);
      gl_Position = uViewProjection * world;
      vNormal = normalize(mat3(uModel) * aNormal);
      vColor = mix(aColor, aColor * vec3(0.72, 1.12, 1.08), uSelected);
    }
  `;
  const fragmentSource = `
    precision mediump float;
    varying vec3 vNormal;
    varying vec3 vColor;
    void main() {
      vec3 light = normalize(vec3(-0.45, 0.85, 0.7));
      float shade = 0.48 + max(dot(normalize(vNormal), light), 0.0) * 0.58;
      gl_FragColor = vec4(vColor * shade, 1.0);
    }
  `;
  function shader(type, source) {
    const item = gl.createShader(type);
    gl.shaderSource(item, source);
    gl.compileShader(item);
    if (!gl.getShaderParameter(item, gl.COMPILE_STATUS)) { const error = new Error(gl.getShaderInfoLog(item)); gl.deleteShader(item); throw error; }
    return item;
  }
  const program = gl.createProgram();
  gl.attachShader(program, shader(gl.VERTEX_SHADER, vertexSource));
  gl.attachShader(program, shader(gl.FRAGMENT_SHADER, fragmentSource));
  gl.linkProgram(program);
  if (!gl.getProgramParameter(program, gl.LINK_STATUS)) { const error = new Error(gl.getProgramInfoLog(program)); gl.deleteProgram(program); throw error; }
  gl.useProgram(program);

  const attributes = {
    position: gl.getAttribLocation(program, "aPosition"),
    normal: gl.getAttribLocation(program, "aNormal"),
    color: gl.getAttribLocation(program, "aColor")
  };
  const uniforms = {
    vp: gl.getUniformLocation(program, "uViewProjection"),
    model: gl.getUniformLocation(program, "uModel")
    , selected: gl.getUniformLocation(program, "uSelected")
  };
  const vertexData = [];
  function tri(a, b, c, color) {
    const ux=b[0]-a[0], uy=b[1]-a[1], uz=b[2]-a[2];
    const vx=c[0]-a[0], vy=c[1]-a[1], vz=c[2]-a[2];
    let nx=uy*vz-uz*vy, ny=uz*vx-ux*vz, nz=ux*vy-uy*vx;
    const length=Math.hypot(nx,ny,nz)||1; nx/=length; ny/=length; nz/=length;
    for (const point of [a,b,c]) vertexData.push(...point,nx,ny,nz,...color);
  }
  function quad(a,b,c,d,color) { tri(a,b,c,color); tri(a,c,d,color); }
  function box(cx,cy,cz,sx,sy,sz,color,variation=0.05) {
    const x=sx/2,y=sy/2,z=sz/2;
    const faces=[
      [[-x,-y,z],[x,-y,z],[x,y,z],[-x,y,z],[0,0,1]],
      [[x,-y,-z],[-x,-y,-z],[-x,y,-z],[x,y,-z],[0,0,-1]],
      [[x,-y,z],[x,-y,-z],[x,y,-z],[x,y,z],[1,0,0]],
      [[-x,-y,-z],[-x,-y,z],[-x,y,z],[-x,y,-z],[-1,0,0]],
      [[-x,y,z],[x,y,z],[x,y,-z],[-x,y,-z],[0,1,0]],
      [[-x,-y,-z],[x,-y,-z],[x,-y,z],[-x,-y,z],[0,-1,0]]
    ];
    faces.forEach((f,i)=>{
      const delta=1+((i%3)-1)*variation;
      const shade=color.map(v=>Math.min(1,v*delta));
      const pts=f.slice(0,4).map(p=>[p[0]+cx,p[1]+cy,p[2]+cz]);
      quad(...pts,shade);
    });
  }
  function cylinder(cx,cy,cz,radius,height,segments,color) {
    for(let i=0;i<segments;i++){
      const a=i*2*Math.PI/segments,b=(i+1)*2*Math.PI/segments;
      const p1=[cx+Math.cos(a)*radius,cy-height/2,cz+Math.sin(a)*radius];
      const p2=[cx+Math.cos(b)*radius,cy-height/2,cz+Math.sin(b)*radius];
      const p3=[cx+Math.cos(b)*radius,cy+height/2,cz+Math.sin(b)*radius];
      const p4=[cx+Math.cos(a)*radius,cy+height/2,cz+Math.sin(a)*radius];
      const tint=1+(i%2)*0.035; quad(p1,p2,p3,p4,color.map(v=>Math.min(1,v*tint)));
      tri([cx,cy-height/2,cz],p2,p1,color.map(v=>v*.8));
      tri([cx,cy+height/2,cz],p4,p3,color.map(v=>Math.min(1,v*1.12)));
    }
  }
  function ring(cx,cy,cz,outer,inner,segments,color) {
    for(let i=0;i<segments;i++){
      const a=i*2*Math.PI/segments,b=(i+1)*2*Math.PI/segments;
      const p=(r,t,y)=>[cx+Math.cos(t)*r,cy+y,cz+Math.sin(t)*r];
      const a0=p(outer,a,-.09),a1=p(outer,b,-.09),a2=p(outer,b,.09),a3=p(outer,a,.09);
      const b0=p(inner,a,-.09),b1=p(inner,b,-.09),b2=p(inner,b,.09),b3=p(inner,a,.09);
      const c=i%3===0?color.map(v=>Math.min(1,v*1.1)):color;
      quad(a0,a1,a2,a3,c); quad(b1,b0,b3,b2,c.map(v=>v*.62));
      quad(a3,a2,b2,b3,c.map(v=>v*.82)); quad(a0,b0,b1,a1,c.map(v=>v*.78));
    }
  }

  const parts=[];
  function addMesh(name){
    const start=vertexData.length/9;
    if(name==="platform"){
      box(0,-.68,0,3.1,.16,2.1,[.24,.31,.34],.025);
      box(0,-.56,0,2.8,.09,1.8,[.35,.39,.38],.02);
    }
    if(name==="crate"){
      box(-.57,-.15,.05,.94,.88,.78,[.58,.42,.28],.07);
      box(-.57,.3,.05,.98,.09,.82,[.44,.32,.23],.035);
      box(-.57,-.15,.46,.12,.75,.035,[.7,.53,.35],.02);
      box(-.57,-.15,-.36,.12,.75,.035,[.7,.53,.35],.02);
      box(-.57,-.15,.49,.34,.22,.04,[.23,.34,.33],.01);
    }
    if(name==="housing"){
      box(.55,-.2,-.04,.74,.74,.68,[.31,.49,.48],.035);
      box(.55,-.2,.32,.42,.34,.09,[.73,.57,.36],.02);
      cylinder(.55,-.2,.40,.075,.035,9,[.94,.76,.48]);
    }
    if(name==="ring"){
      ring(.12,.07,-.22,.70,.53,20,[.58,.62,.57]);
      for(let i=0;i<10;i++){
        const a=2*Math.PI*i/10;
        cylinder(.12+Math.cos(a)*.615,.07+Math.sin(a)*.615,-.22,.045,.23,7,[.78,.64,.43]);
      }
    }
    const data=new Float32Array(vertexData.slice(start*9));
    const buffer=gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER,buffer); gl.bufferData(gl.ARRAY_BUFFER,data,gl.STATIC_DRAW);
    parts.push({buffer,count:data.length/9,name, transform:{x:0,y:0,rotation:0,scale:1}});
  }
  ["platform","crate","housing","ring"].forEach(addMesh);

  function perspective(fov,aspect,near,far){
    const f=1/Math.tan(fov/2),nf=1/(near-far);
    return new Float32Array([f/aspect,0,0,0,0,f,0,0,0,0,(far+near)*nf,-1,0,0,2*far*near*nf,0]);
  }
  function multiply(a,b){
    const out=new Float32Array(16);
    for(let c=0;c<4;c++)for(let r=0;r<4;r++)out[c*4+r]=a[r]*b[c*4]+a[4+r]*b[c*4+1]+a[8+r]*b[c*4+2]+a[12+r]*b[c*4+3];
    return out;
  }
  function rotateX(a){const c=Math.cos(a),s=Math.sin(a);return new Float32Array([1,0,0,0,0,c,s,0,0,-s,c,0,0,0,0,1]);}
  function rotateY(a){const c=Math.cos(a),s=Math.sin(a);return new Float32Array([c,0,-s,0,0,1,0,0,s,0,c,0,0,0,0,1]);}
  function translate(x,y,z){return new Float32Array([1,0,0,0,0,1,0,0,0,0,1,0,x,y,z,1]);}
  function lookAt(eye,center,up){
    const norm=v=>{const l=Math.hypot(...v)||1;return v.map(n=>n/l)};
    const cross=(a,b)=>[a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]];
    const sub=(a,b)=>a.map((x,i)=>x-b[i]);
    const z=norm(sub(eye,center)),x=norm(cross(up,z)),y=cross(z,x);
    return new Float32Array([x[0],y[0],z[0],0,x[1],y[1],z[1],0,x[2],y[2],z[2],0,-x.reduce((s,v,i)=>s+v*eye[i],0),-y.reduce((s,v,i)=>s+v*eye[i],0),-z.reduce((s,v,i)=>s+v*eye[i],0),1]);
  }
  let yaw=Number.isFinite(restored.yaw)?restored.yaw:-.36,pitch=Number.isFinite(restored.pitch)?restored.pitch:.22,panX=Number.isFinite(restored.panX)?restored.panX:0,panY=Number.isFinite(restored.panY)?restored.panY:0,drag=false,dragMode="orbit",lastX=0,lastY=0,selectedName=restored.selectedName||objectSelect?.value||"crate";
  if(objectSelect)objectSelect.value=selectedName;
  const selectedPart=()=>parts.find(part=>part.name===selectedName);
  function syncObjectControls(){
    const t=selectedPart()?.transform;if(!t)return;
    objectControls.x.value=t.x;objectControls.y.value=t.y;objectControls.rotation.value=t.rotation;objectControls.scale.value=t.scale;
    if(selectedLabel)selectedLabel.textContent=`${selectedName.toUpperCase()} · /BlackLionAssetArchive`;
  }
  if(objectSelect)on(objectSelect,"change",()=>{selectedName=objectSelect.value;syncObjectControls();draw(performance.now());});
  for(const [key,input] of Object.entries(objectControls)) if(input)on(input,"input",()=>{const t=selectedPart()?.transform;if(!t)return;t[key]=Number(input.value);draw(performance.now());});
  if(resetObject)on(resetObject,"click",()=>{const t=selectedPart()?.transform;if(!t)return;Object.assign(t,{x:0,y:0,rotation:0,scale:1});syncObjectControls();draw(performance.now());});
  for(const part of parts)Object.assign(part.transform,restored.transforms?.[part.name]||{});
  syncObjectControls();
  let frame=0;
  const reduced=window.matchMedia("(prefers-reduced-motion: reduce)").matches;
  function resize(){
    const cap=qualityMode==="auto"?adaptiveDpr:qualityCaps[qualityMode];
    const dpr=Math.min(deviceDpr,cap),rect=canvas.getBoundingClientRect();
    const w=Math.max(1,Math.floor(rect.width*dpr)),h=Math.max(1,Math.floor(rect.height*dpr));
    if(canvas.width!==w||canvas.height!==h){canvas.width=w;canvas.height=h;gl.viewport(0,0,w,h);}
  }
  let frameHandle=0,running=false,inViewport=true,lastRender=0,sampleStart=0,sampleFrames=0,observer=null;
  function reportStats(now){
    sampleFrames++;
    if(!sampleStart)sampleStart=now;
    if(now-sampleStart<1200)return;
    const fps=frameRateFromSamples(sampleFrames,now-sampleStart);
    if(fps===null){sampleStart=now;sampleFrames=0;return;}
    if(qualityMode==="auto"){
      adaptiveDpr=nextAdaptiveDpr(adaptiveDpr,fps,deviceDpr)??adaptiveDpr;
    }
    const actual=Math.round(canvas.width/Math.max(1,canvas.clientWidth)*100)/100;
    if(stats)stats.textContent=`${qualityMode==="auto"?"Auto":qualityMode} · ${Math.round(fps)} fps · ${actual}×`;
    sampleStart=now;sampleFrames=0;
  }
  function onAnimationFrame(now){draw(now,true);}
  function draw(now,measureFrame=false){
    if(now-lastRender<32){if(running)frameHandle=requestAnimationFrame(onAnimationFrame);return;}
    lastRender=now;
    resize();
    gl.enable(gl.DEPTH_TEST);gl.enable(gl.CULL_FACE);gl.clearColor(0,0,0,0);gl.clear(gl.COLOR_BUFFER_BIT|gl.DEPTH_BUFFER_BIT);
    if(!drag&&!reduced)yaw+=.00026*(now-frame||16);frame=now;
    const viewProjection=multiply(perspective(.78,canvas.width/canvas.height,.1,40),lookAt([panX,1.45+panY,5.4],[panX,-.08+panY,0],[0,1,0]));
    gl.uniformMatrix4fv(uniforms.vp,false,viewProjection);
    const base=multiply(translate(0,0,0),multiply(rotateY(yaw),rotateX(pitch)));
    const rotationMatrix=angle=>new Float32Array([Math.cos(angle),0,-Math.sin(angle),0,0,1,0,0,Math.sin(angle),0,Math.cos(angle),0,0,0,0,1]);
    for(const part of parts){
      gl.bindBuffer(gl.ARRAY_BUFFER,part.buffer);
      const stride=9*4;
      gl.enableVertexAttribArray(attributes.position);gl.vertexAttribPointer(attributes.position,3,gl.FLOAT,false,stride,0);
      gl.enableVertexAttribArray(attributes.normal);gl.vertexAttribPointer(attributes.normal,3,gl.FLOAT,false,stride,12);
      gl.enableVertexAttribArray(attributes.color);gl.vertexAttribPointer(attributes.color,3,gl.FLOAT,false,stride,24);
      const t=part.transform;
      const local=multiply(translate(t.x,t.y,0),multiply(rotationMatrix(t.rotation*Math.PI/180),new Float32Array([t.scale,0,0,0,0,t.scale,0,0,0,0,t.scale,0,0,0,0,1])));
      gl.uniformMatrix4fv(uniforms.model,false,multiply(base,local));gl.uniform1f(uniforms.selected,part.name===selectedName?1:0);gl.drawArrays(gl.TRIANGLES,0,part.count);
    }
    if(measureFrame)reportStats(now);
    if(running&&!reduced)frameHandle=requestAnimationFrame(onAnimationFrame);
  }
  if(qualitySelect)on(qualitySelect,"change",()=>{
    qualityMode=qualitySelect.value;
    ctx.preferences.set("asset-atlas-preview-quality",qualityMode);
    if(stats)stats.textContent=`${qualityMode} · applying`;
    resize();draw(performance.now());
  });
  function start(){
    if(document.hidden||!inViewport){if(stats)stats.textContent=`${qualityMode==="auto"?"Auto":qualityMode} · paused`;return;}
    sampleStart=0;sampleFrames=0;
    if(reduced){if(stats)stats.textContent="Static preview · reduced motion";requestAnimationFrame(now=>draw(now,false));return;}
    if(!running){running=true;frameHandle=requestAnimationFrame(onAnimationFrame);}
  }
  function stop(){running=false;cancelAnimationFrame(frameHandle);sampleStart=0;sampleFrames=0;if(stats)stats.textContent=`${qualityMode==="auto"?"Auto":qualityMode} · paused`;}
  if("IntersectionObserver" in window){
    observer=new IntersectionObserver(entries=>{
      inViewport=entries.some(entry=>entry.isIntersecting);
      if(inViewport)start();else stop();
    },{threshold:.05});
    observer.observe(canvas);
  }
  on(document,"visibilitychange",()=>document.hidden?stop():start());
  on(canvas,"contextmenu",e=>e.preventDefault());
  on(canvas,"pointerdown",e=>{if(e.button!==0&&e.button!==1)return;drag=true;dragMode=e.button===1?"pan":"orbit";if(dragMode==="pan")e.preventDefault();lastX=e.clientX;lastY=e.clientY;canvas.setPointerCapture(e.pointerId);});
  on(canvas,"pointermove",e=>{if(!drag)return;const dx=e.clientX-lastX,dy=e.clientY-lastY;if(dragMode==="pan"){panX-=dx/Math.max(1,canvas.clientWidth)*4.2;panY+=dy/Math.max(1,canvas.clientHeight)*3.2;}else{yaw+=dx*.009;pitch=Math.max(-.65,Math.min(.72,pitch+dy*.008));}lastX=e.clientX;lastY=e.clientY;if(reduced)requestAnimationFrame(now=>draw(now,false));});
  on(canvas,"pointerup",()=>{drag=false;dragMode="orbit";});
  on(canvas,"pointercancel",()=>{drag=false;dragMode="orbit";});
  if(reset)on(reset,"click",()=>{yaw=-.36;pitch=.22;panX=0;panY=0;if(reduced)requestAnimationFrame(draw);});
  if(!("IntersectionObserver" in window)){inViewport=true;start();}
  else if(document.hidden)start();
  return {
    snapshot: () => ({ qualityMode, yaw, pitch, panX, panY, selectedName, transforms: Object.fromEntries(parts.map(part => [part.name, { ...part.transform }])) }),
    canReload: () => !drag && !document.hidden && ![qualitySelect, objectSelect, ...Object.values(objectControls)].includes(document.activeElement),
    dispose: () => { running=false; cancelAnimationFrame(frameHandle); observer?.disconnect(); listeners.abort(); for(const part of parts)gl.deleteBuffer(part.buffer); gl.deleteProgram(program); }
  };
}
