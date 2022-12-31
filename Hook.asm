.data
extern hookRetAddress : qword
extern damage_tmp : dword
extern playerAddress : qword

.code
recordPlayerDamage proc

push rbx
mov rax,[rsi+10h]
cmp rax, playerAddress
je addDmg
cmp dword ptr [rsi+24h],00
jne fend
mov rbx, playerAddress
mov rbx, [rbx+1168h]
cmp rax, rbx
jne fend
addDmg:
movd ecx,xmm0
addss xmm0,damage_tmp
movss damage_tmp,xmm0
movd xmm0,ecx
fend:
addss xmm0,dword ptr [rdi+000001FCh]
pop rbx
jmp hookRetAddress

recordPlayerDamage ENDP
END