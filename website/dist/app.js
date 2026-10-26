(() => {
  const canvas = document.getElementById("asset-scene");
  const reset = document.getElementById("reset-view");
  const copyButton = document.getElementById("copy-manifest");
  if (!canvas) return;

  const manifest = {
    schema: "blacklion.asset-archive.item.v1",
    id: "relay_housing_v1",
    project_id: "studio_game_alpha",
    name: "Relay Housing",
    kind: "object",
    preview_assets: ["/Game/Factory/SM_RelayHousing.SM_RelayHousing"],
    todos: ["Check floor contact in archive"],
    pbr_channels: ["base_color", "normal"]
  };

  copyButton?.addEventListener("click", async () => {
    try {
      await navigator.clipboard.writeText(JSON.stringify(manifest, null, 2));
      copyButton.textContent = "Copied";
      setTimeout(() => (copyButton.textContent = "Copy example"), 1600);
    } catch {
      copyButton.textContent = "Select code to copy";
      setTimeout(() => (copyButton.textContent = "Copy example"), 1800);
    }
  });

  const gl = canvas.getContext("webgl", { alpha: true, antialias: true, powerPreference: "low-power" });
  if (!gl) {
    const ctx = canvas.getContext("2d");
    if (ctx) {
      ctx.fillStyle = "#91dfcf";
      ctx.font = "14px system-ui";
      ctx.fillText("WebGL is unavailable in this browser", 24, 38);
    }
    return;
  }

  const vertexSource = `
    attribute vec3 aPosition;
    attribute vec3 aNormal;
    attribute vec3 aColor;
    uniform mat4 uViewProjection;
    uniform mat4 uModel;
    varying vec3 vNormal;
    varying vec3 vColor;
    void main() {
      vec4 world = uModel * vec4(aPosition, 1.0);
      gl_Position = uViewProjection * world;
      vNormal = normalize(mat3(uModel) * aNormal);
      vColor = aColor;
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
    if (!gl.getShaderParameter(item, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(item));
    return item;
  }
  const program = gl.createProgram();
  gl.attachShader(program, shader(gl.VERTEX_SHADER, vertexSource));
  gl.attachShader(program, shader(gl.FRAGMENT_SHADER, fragmentSource));
  gl.linkProgram(program);
  if (!gl.getProgramParameter(program, gl.LINK_STATUS)) return;
  gl.useProgram(program);

  const attributes = {
    position: gl.getAttribLocation(program, "aPosition"),
    normal: gl.getAttribLocation(program, "aNormal"),
    color: gl.getAttribLocation(program, "aColor")
  };
  const uniforms = {
    vp: gl.getUniformLocation(program, "uViewProjection"),
    model: gl.getUniformLocation(program, "uModel")
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
    parts.push({buffer,count:data.length/9,name});
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
  let yaw=-.36,pitch=.22,drag=false,lastX=0,lastY=0;
  let frame=0;
  const reduced=window.matchMedia("(prefers-reduced-motion: reduce)").matches;
  function resize(){
    const dpr=Math.min(window.devicePixelRatio||1,1.6),rect=canvas.getBoundingClientRect();
    const w=Math.max(1,Math.floor(rect.width*dpr)),h=Math.max(1,Math.floor(rect.height*dpr));
    if(canvas.width!==w||canvas.height!==h){canvas.width=w;canvas.height=h;gl.viewport(0,0,w,h);}
  }
  let frameHandle=0,running=false,inViewport=true;
  function draw(now){
    resize();
    gl.enable(gl.DEPTH_TEST);gl.enable(gl.CULL_FACE);gl.clearColor(0,0,0,0);gl.clear(gl.COLOR_BUFFER_BIT|gl.DEPTH_BUFFER_BIT);
    if(!drag&&!reduced)yaw+=.00026*(now-frame||16);frame=now;
    const viewProjection=multiply(perspective(.78,canvas.width/canvas.height,.1,40),lookAt([0,1.45,5.4],[0,-.08,0],[0,1,0]));
    gl.uniformMatrix4fv(uniforms.vp,false,viewProjection);
    const base=multiply(translate(0,0,0),multiply(rotateY(yaw),rotateX(pitch)));
    for(const part of parts){
      gl.bindBuffer(gl.ARRAY_BUFFER,part.buffer);
      const stride=9*4;
      gl.enableVertexAttribArray(attributes.position);gl.vertexAttribPointer(attributes.position,3,gl.FLOAT,false,stride,0);
      gl.enableVertexAttribArray(attributes.normal);gl.vertexAttribPointer(attributes.normal,3,gl.FLOAT,false,stride,12);
      gl.enableVertexAttribArray(attributes.color);gl.vertexAttribPointer(attributes.color,3,gl.FLOAT,false,stride,24);
      gl.uniformMatrix4fv(uniforms.model,false,base);gl.drawArrays(gl.TRIANGLES,0,part.count);
    }
    if(running&&!reduced)frameHandle=requestAnimationFrame(draw);
  }
  function start(){
    if(document.hidden||!inViewport)return;
    if(reduced){requestAnimationFrame(draw);return;}
    if(!running){running=true;frameHandle=requestAnimationFrame(draw);}
  }
  function stop(){running=false;cancelAnimationFrame(frameHandle);}
  if("IntersectionObserver" in window){
    const observer=new IntersectionObserver(entries=>{
      inViewport=entries.some(entry=>entry.isIntersecting);
      if(inViewport)start();else stop();
    },{threshold:.05});
    observer.observe(canvas);
  }
  document.addEventListener("visibilitychange",()=>document.hidden?stop():start());
  canvas.addEventListener("pointerdown",e=>{drag=true;lastX=e.clientX;lastY=e.clientY;canvas.setPointerCapture(e.pointerId);});
  canvas.addEventListener("pointermove",e=>{if(!drag)return;yaw+=(e.clientX-lastX)*.009;pitch=Math.max(-.65,Math.min(.72,pitch+(e.clientY-lastY)*.008));lastX=e.clientX;lastY=e.clientY;if(reduced)requestAnimationFrame(draw);});
  canvas.addEventListener("pointerup",()=>{drag=false;});
  canvas.addEventListener("pointercancel",()=>{drag=false;});
  reset?.addEventListener("click",()=>{yaw=-.36;pitch=.22;if(reduced)requestAnimationFrame(draw);});
  if(!("IntersectionObserver" in window)){inViewport=true;start();}
})();
