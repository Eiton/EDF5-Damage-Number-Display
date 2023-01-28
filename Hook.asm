.data
extern hookRetAddress : qword
extern playerAddress : qword
extern add_damage : proto

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
push rdx
mov rdx,rsi
movd ecx,xmm0
push rcx
push r8
mov r8, rdi
sub rsp, 20h
call add_damage
add rsp, 20h
pop r8
pop rcx
pop rdx
movd xmm0,ecx
fend:
addss xmm0,dword ptr [rdi+000001FCh]
pop rbx
jmp hookRetAddress

recordPlayerDamage ENDP
END