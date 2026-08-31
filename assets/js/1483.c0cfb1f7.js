"use strict";(self.webpackChunkembedded_linux_ui_course_docs=self.webpackChunkembedded_linux_ui_course_docs||[]).push([["1483"],{6806(e,t,a){a.d(t,{diagram:()=>D});var i=a(7454),l=a(5637),r=a(1916),n=a(4599),s=a(1293),o=a(6827),d=a(8731),c=a(7829),p=n.UI.pie,h={sections:new Map,showData:!1,config:p},g=h.sections,u=h.showData,f=structuredClone(p),m=(0,o.K)(()=>structuredClone(f),"getConfig"),x=(0,o.K)(()=>{g=new Map,u=h.showData,(0,n.IU)()},"clear"),$=(0,o.K)(({label:e,value:t})=>{if(t<0)throw Error(`"${e}" has invalid value: ${t}. Negative values are not allowed in pie charts. All slice values must be >= 0.`);g.has(e)||(g.set(e,t),s.R.debug(`added new section: ${e}, with value: ${t}`))},"addSection"),w=(0,o.K)(()=>g,"getSections"),S=(0,o.K)(e=>{u=e},"setShowData"),b=(0,o.K)(()=>u,"getShowData"),y={getConfig:m,clear:x,setDiagramTitle:n.ke,getDiagramTitle:n.ab,setAccTitle:n.SV,getAccTitle:n.iN,setAccDescription:n.EI,getAccDescription:n.m7,addSection:$,getSections:w,setShowData:S,getShowData:b},k=(0,o.K)((e,t)=>{(0,i.S)(e,t),t.setShowData(e.showData),e.sections.map(t.addSection)},"populateDb"),C={parse:(0,o.K)(async e=>{let t=await (0,d.qg)("pie",e);s.R.debug(t),k(t,y)},"parse")},v=(0,o.K)(e=>`
  .pieCircle{
    stroke: ${e.pieStrokeColor};
    stroke-width : ${e.pieStrokeWidth};
    opacity : ${e.pieOpacity};
  }
  .pieCircle.highlighted{
    scale: 1.05;
    opacity: 1;
  }
  .pieCircle.highlightedOnHover:hover{
    transition-duration: 250ms;
    scale: 1.05;
    opacity: 1;
  }
  .pieOuterCircle{
    stroke: ${e.pieOuterStrokeColor};
    stroke-width: ${e.pieOuterStrokeWidth};
    fill: none;
  }
  .pieTitleText {
    text-anchor: middle;
    font-size: ${e.pieTitleTextSize};
    fill: ${e.pieTitleTextColor};
    font-family: ${e.fontFamily};
  }
  .slice {
    font-family: ${e.fontFamily};
    fill: ${e.pieSectionTextColor};
    font-size:${e.pieSectionTextSize};
    // fill: white;
  }
  .legend text {
    fill: ${e.pieLegendTextColor};
    font-family: ${e.fontFamily};
    font-size: ${e.pieLegendTextSize};
  }
`,"getStyles"),T=(0,o.K)(e=>{let t=[...e.values()].reduce((e,t)=>e+t,0),a=[...e.entries()].map(([e,t])=>({label:e,value:t})).filter(e=>e.value/t*100>=1);return(0,c.rLf)().value(e=>e.value).sort(null)(a)},"createPieArcs"),D={parser:C,db:y,renderer:{draw:(0,o.K)((e,t,a,i)=>{s.R.debug("rendering pie chart\n"+e);let o=i.db,d=(0,n.D7)(),p=(0,r.$t)(o.getConfig(),d.pie),h=(0,l.D)(t),g=h.append("g");g.attr("transform","translate(225,225)");let{themeVariables:u}=d,[f]=(0,r.I5)(u.pieOuterStrokeWidth);f??=2;let m=p.legendPosition,x=p.textPosition,$=p.donutHole>0&&p.donutHole<=.9?p.donutHole:0,w=(0,c.JLW)().innerRadius(185*$).outerRadius(185),S=(0,c.JLW)().innerRadius(185*x).outerRadius(185*x),b=g.append("g");b.append("circle").attr("cx",0).attr("cy",0).attr("r",185+f/2).attr("class","pieOuterCircle");let y=o.getSections(),k=T(y),C=[u.pie1,u.pie2,u.pie3,u.pie4,u.pie5,u.pie6,u.pie7,u.pie8,u.pie9,u.pie10,u.pie11,u.pie12],v=0;y.forEach(e=>{v+=e});let D=k.filter(e=>"0"!==(e.data.value/v*100).toFixed(0)),K=(0,c.UMr)(C).domain([...y.keys()]);b.selectAll("mySlices").data(D).enter().append("path").attr("d",w).attr("fill",e=>K(e.data.label)).attr("class",e=>{let t="pieCircle";return"hover"===p.highlightSlice?t+=" highlightedOnHover":p.highlightSlice===e.data.label&&(t+=" highlighted"),t}),b.selectAll("mySlices").data(D).enter().append("text").text(e=>(e.data.value/v*100).toFixed(0)+"%").attr("transform",e=>"translate("+S.centroid(e)+")").style("text-anchor","middle").attr("class","slice");let A=g.append("text").text(o.getDiagramTitle()).attr("x",0).attr("y",-200).attr("class","pieTitleText"),R=[...y.entries()].map(([e,t])=>({label:e,value:t})),O=g.selectAll(".legend").data(R).enter().append("g").attr("class","legend");O.append("rect").attr("width",18).attr("height",18).style("fill",e=>K(e.label)).style("stroke",e=>K(e.label)),O.append("text").attr("x",22).attr("y",14).text(e=>o.getShowData()?`${e.label} [${e.value}]`:e.label);let _=Math.max(...O.selectAll("text").nodes().map(e=>e?.getBoundingClientRect().width??0)),M=450,z=490,W=22*R.length;switch(m){case"center":O.attr("transform",(e,t)=>"translate("+(-_/2-22)+","+(22*t-22*R.length/2)+")");break;case"top":M+=W,O.attr("transform",(e,t)=>`translate(${-_/2-22}, ${22*t-185})`),b.attr("transform",()=>`translate(0, ${W+22})`);break;case"bottom":M+=W,O.attr("transform",(e,t)=>"translate("+(-_/2-22)+","+(22*t- -207)+")");break;case"left":z+=22+_,O.attr("transform",(e,t)=>"translate(-207,"+(22*t-22*R.length/2)+")"),b.attr("transform",()=>`translate(${_+18+4}, 0)`);break;default:z+=22+_,O.attr("transform",(e,t)=>"translate(216,"+(22*t-22*R.length/2)+")")}let F=A.node()?.getBoundingClientRect().width??0,H=Math.min(0,225-F/2),L=Math.max(z,225+F/2)-H;h.attr("viewBox",`${H} 0 ${L} ${M}`),(0,n.a$)(h,M,L,p.useMaxWidth)},"draw")},styles:v}}}]);