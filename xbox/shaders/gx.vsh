; gx.vsh — the one NV2A vertex program for the GX backend (xbox_gl_nv2a.c).
; Reproduces pc/shaders/default.vert + the rasterized-colour half of
; default.frag, per vertex (the GameCube itself lights per vertex).
; Assemble: tools/xbox/build_shaders.sh  (nv2a-vsh, no Cg needed)
;
; NV2A has temporaries r0-r11 only (r12 aliases oPos).
; Rule of the NV2A ISA: at most ONE constant (c) and ONE input (v) register
; read per instruction. Every flag below is 0.0 or 1.0 so selection is a lerp.
;
; Inputs: iPos (v0) iNormal (v2) iDiffuse (v3, UB_OGL rgba) iTex0 (v9)

#proj     matrix4 96
; c[100]: 3 rows of the GX position matrix
; c[103]: 3 rows of the normal matrix (w = 0)
#k        vector  106
#matc     vector  107
#ambc     vector  108
#flag0    vector  109
#flag1    vector  110
#fog      vector  111
; c[112]: light 0..7 direction (normalized, masked-out = 0), 112..119
; c[120]: light 0..7 colour (masked-out = 0), 120..127
; c[128]: stage 0: row0, row1, (src_is_normal,0,0,0)  128..130
; c[131]: stage 1  131..133
; c[134]: stage 2  134..136

; ---- position ---------------------------------------------------------
dp4 r1.x, iPos, c[100]
dp4 r1.y, iPos, c[101]
dp4 r1.z, iPos, c[102]
mov r1.w, #k.y
%matmul4x4 r2 r1 #proj
rcp r3.x, r2.w
mul oPos.xyz, r2, r3.x
mov oPos.w, r2.w

; ---- normal (eye space, normalized) -----------------------------------
dp3 r4.x, iNormal, c[103]
dp3 r4.y, iNormal, c[104]
dp3 r4.z, iNormal, c[105]
dp3 r5.x, r4, r4
rsq r5.x, r5.x
mul r4.xyz, r4, r5.x
mov r4.w, #k.y

; ---- colour channel 0 -------------------------------------------------
; mat = lerp(matc, vcol, flag0.x) ; amb = lerp(ambc, vcol, flag0.y)
mov r0, iDiffuse
add r6, r0, -#matc
mov r7, #flag0.x
mul r6, r6, r7
add r6, r6, #matc
add r7, r0, -#ambc
mov r8, #flag0.y
mul r7, r7, r8
add r7, r7, #ambc
; accum = amb + sum_i max(0, N.L_i) * col_i
mov r8, r7
dp3 r9.x, r4, c[112]
max r9.x, r9.x, #k.x
mad r8.xyz, r9.x, c[120], r8
dp3 r9.x, r4, c[113]
max r9.x, r9.x, #k.x
mad r8.xyz, r9.x, c[121], r8
dp3 r9.x, r4, c[114]
max r9.x, r9.x, #k.x
mad r8.xyz, r9.x, c[122], r8
dp3 r9.x, r4, c[115]
max r9.x, r9.x, #k.x
mad r8.xyz, r9.x, c[123], r8
dp3 r9.x, r4, c[116]
max r9.x, r9.x, #k.x
mad r8.xyz, r9.x, c[124], r8
dp3 r9.x, r4, c[117]
max r9.x, r9.x, #k.x
mad r8.xyz, r9.x, c[125], r8
dp3 r9.x, r4, c[118]
max r9.x, r9.x, #k.x
mad r8.xyz, r9.x, c[126], r8
dp3 r9.x, r4, c[119]
max r9.x, r9.x, #k.x
mad r8.xyz, r9.x, c[127], r8
max r8, r8, #k.x
min r8, r8, #k.y
; rgb = lerp(mat, mat * accum, lighting)
mul r9.xyz, r6, r8
add r9.xyz, r9, -r6
mov r10, #flag0.z
mad r9.xyz, r9, r10, r6
; alpha: matA = lerp(matc.a, v.a, flag1.x) ; a = lerp(matA, matA*ambc.a, flag1.y)
add r10.x, r0.w, -#matc.w
mov r11.x, #flag1.x
mul r10.x, r10.x, r11.x
add r10.x, r10.x, #matc.w
mul r10.y, r10.x, #ambc.w
add r10.y, r10.y, -r10.x
mov r11.x, #flag1.y
mad r9.w, r10.y, r11.x, r10.x
; channels off -> white
add r9, r9, -#k.y
mov r10, #flag0.w
mad r9, r9, r10, #k.y
mov oDiffuse, r9

; ---- fog factor -> specular alpha (read by the final combiner) ---------
add r11.x, -r1.z, -#fog.x
mul r11.x, r11.x, #fog.y
max r11.x, r11.x, #k.x
min r11.x, r11.x, #k.y
mul oSpecular.w, r11.x, #flag1.z
mov oSpecular.xyz, #k.x

; ---- texture coordinates: stage s = texgen(tc0 | normal) * rows ------
mov r5, iTex0
mov r5.zw, c[106].xxxy
add r6, r4, -r5
; stage 0
mov r7, c[130].x
mad r8, r6, r7, r5
dp4 oTex0.x, r8, c[128]
dp4 oTex0.y, r8, c[129]
mov oTex0.zw, c[106].xxxy
; stage 1
mov r7, c[133].x
mad r8, r6, r7, r5
dp4 oTex1.x, r8, c[131]
dp4 oTex1.y, r8, c[132]
mov oTex1.zw, c[106].xxxy
; stage 2
mov r7, c[136].x
mad r8, r6, r7, r5
dp4 oTex2.x, r8, c[134]
dp4 oTex2.y, r8, c[135]
mov oTex2.zw, c[106].xxxy
mov oTex3, c[106].xxxy
