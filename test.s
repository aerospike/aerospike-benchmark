
target/bin/cycle_test:     file format elf64-x86-64


Disassembly of section .init:

0000000000001000 <_init>:
    1000:	f3 0f 1e fa          	endbr64 
    1004:	48 83 ec 08          	sub    $0x8,%rsp
    1008:	48 8b 05 e1 2f 00 00 	mov    0x2fe1(%rip),%rax        # 3ff0 <__gmon_start__>
    100f:	48 85 c0             	test   %rax,%rax
    1012:	74 02                	je     1016 <_init+0x16>
    1014:	ff d0                	callq  *%rax
    1016:	48 83 c4 08          	add    $0x8,%rsp
    101a:	c3                   	retq   

Disassembly of section .plt:

0000000000001020 <.plt>:
    1020:	ff 35 e2 2f 00 00    	pushq  0x2fe2(%rip)        # 4008 <_GLOBAL_OFFSET_TABLE_+0x8>
    1026:	ff 25 e4 2f 00 00    	jmpq   *0x2fe4(%rip)        # 4010 <_GLOBAL_OFFSET_TABLE_+0x10>
    102c:	0f 1f 40 00          	nopl   0x0(%rax)

0000000000001030 <printf@plt>:
    1030:	ff 25 e2 2f 00 00    	jmpq   *0x2fe2(%rip)        # 4018 <printf@GLIBC_2.2.5>
    1036:	68 00 00 00 00       	pushq  $0x0
    103b:	e9 e0 ff ff ff       	jmpq   1020 <.plt>

Disassembly of section .text:

0000000000001040 <main>:
    1040:	53                   	push   %rbx
    1041:	89 fb                	mov    %edi,%ebx
    1043:	0f 31                	rdtsc  
    1045:	48 89 c1             	mov    %rax,%rcx
    1048:	48 c1 e2 20          	shl    $0x20,%rdx
    104c:	48 09 d1             	or     %rdx,%rcx
    104f:	0f 31                	rdtsc  
    1051:	48 c1 e2 20          	shl    $0x20,%rdx
    1055:	48 09 d0             	or     %rdx,%rax
    1058:	48 29 c8             	sub    %rcx,%rax
    105b:	48 89 c6             	mov    %rax,%rsi
    105e:	48 8d 3d 9f 0f 00 00 	lea    0xf9f(%rip),%rdi        # 2004 <_IO_stdin_used+0x4>
    1065:	31 c0                	xor    %eax,%eax
    1067:	e8 c4 ff ff ff       	callq  1030 <printf@plt>
    106c:	8d 43 01             	lea    0x1(%rbx),%eax
    106f:	5b                   	pop    %rbx
    1070:	c3                   	retq   
    1071:	66 2e 0f 1f 84 00 00 	nopw   %cs:0x0(%rax,%rax,1)
    1078:	00 00 00 
    107b:	0f 1f 44 00 00       	nopl   0x0(%rax,%rax,1)

0000000000001080 <_start>:
    1080:	f3 0f 1e fa          	endbr64 
    1084:	31 ed                	xor    %ebp,%ebp
    1086:	49 89 d1             	mov    %rdx,%r9
    1089:	5e                   	pop    %rsi
    108a:	48 89 e2             	mov    %rsp,%rdx
    108d:	48 83 e4 f0          	and    $0xfffffffffffffff0,%rsp
    1091:	50                   	push   %rax
    1092:	54                   	push   %rsp
    1093:	4c 8d 05 56 01 00 00 	lea    0x156(%rip),%r8        # 11f0 <__libc_csu_fini>
    109a:	48 8d 0d df 00 00 00 	lea    0xdf(%rip),%rcx        # 1180 <__libc_csu_init>
    10a1:	48 8d 3d 98 ff ff ff 	lea    -0x68(%rip),%rdi        # 1040 <main>
    10a8:	ff 15 3a 2f 00 00    	callq  *0x2f3a(%rip)        # 3fe8 <__libc_start_main@GLIBC_2.2.5>
    10ae:	f4                   	hlt    
    10af:	90                   	nop

00000000000010b0 <deregister_tm_clones>:
    10b0:	48 8d 3d 79 2f 00 00 	lea    0x2f79(%rip),%rdi        # 4030 <__TMC_END__>
    10b7:	48 8d 05 72 2f 00 00 	lea    0x2f72(%rip),%rax        # 4030 <__TMC_END__>
    10be:	48 39 f8             	cmp    %rdi,%rax
    10c1:	74 15                	je     10d8 <deregister_tm_clones+0x28>
    10c3:	48 8b 05 16 2f 00 00 	mov    0x2f16(%rip),%rax        # 3fe0 <_ITM_deregisterTMCloneTable>
    10ca:	48 85 c0             	test   %rax,%rax
    10cd:	74 09                	je     10d8 <deregister_tm_clones+0x28>
    10cf:	ff e0                	jmpq   *%rax
    10d1:	0f 1f 80 00 00 00 00 	nopl   0x0(%rax)
    10d8:	c3                   	retq   
    10d9:	0f 1f 80 00 00 00 00 	nopl   0x0(%rax)

00000000000010e0 <register_tm_clones>:
    10e0:	48 8d 3d 49 2f 00 00 	lea    0x2f49(%rip),%rdi        # 4030 <__TMC_END__>
    10e7:	48 8d 35 42 2f 00 00 	lea    0x2f42(%rip),%rsi        # 4030 <__TMC_END__>
    10ee:	48 29 fe             	sub    %rdi,%rsi
    10f1:	48 89 f0             	mov    %rsi,%rax
    10f4:	48 c1 ee 3f          	shr    $0x3f,%rsi
    10f8:	48 c1 f8 03          	sar    $0x3,%rax
    10fc:	48 01 c6             	add    %rax,%rsi
    10ff:	48 d1 fe             	sar    %rsi
    1102:	74 14                	je     1118 <register_tm_clones+0x38>
    1104:	48 8b 05 ed 2e 00 00 	mov    0x2eed(%rip),%rax        # 3ff8 <_ITM_registerTMCloneTable>
    110b:	48 85 c0             	test   %rax,%rax
    110e:	74 08                	je     1118 <register_tm_clones+0x38>
    1110:	ff e0                	jmpq   *%rax
    1112:	66 0f 1f 44 00 00    	nopw   0x0(%rax,%rax,1)
    1118:	c3                   	retq   
    1119:	0f 1f 80 00 00 00 00 	nopl   0x0(%rax)

0000000000001120 <__do_global_dtors_aux>:
    1120:	f3 0f 1e fa          	endbr64 
    1124:	80 3d 05 2f 00 00 00 	cmpb   $0x0,0x2f05(%rip)        # 4030 <__TMC_END__>
    112b:	75 33                	jne    1160 <__do_global_dtors_aux+0x40>
    112d:	55                   	push   %rbp
    112e:	48 83 3d a2 2e 00 00 	cmpq   $0x0,0x2ea2(%rip)        # 3fd8 <__cxa_finalize@GLIBC_2.2.5>
    1135:	00 
    1136:	48 89 e5             	mov    %rsp,%rbp
    1139:	74 0d                	je     1148 <__do_global_dtors_aux+0x28>
    113b:	48 8b 3d e6 2e 00 00 	mov    0x2ee6(%rip),%rdi        # 4028 <__dso_handle>
    1142:	ff 15 90 2e 00 00    	callq  *0x2e90(%rip)        # 3fd8 <__cxa_finalize@GLIBC_2.2.5>
    1148:	e8 63 ff ff ff       	callq  10b0 <deregister_tm_clones>
    114d:	c6 05 dc 2e 00 00 01 	movb   $0x1,0x2edc(%rip)        # 4030 <__TMC_END__>
    1154:	5d                   	pop    %rbp
    1155:	c3                   	retq   
    1156:	66 2e 0f 1f 84 00 00 	nopw   %cs:0x0(%rax,%rax,1)
    115d:	00 00 00 
    1160:	c3                   	retq   
    1161:	66 66 2e 0f 1f 84 00 	data16 nopw %cs:0x0(%rax,%rax,1)
    1168:	00 00 00 00 
    116c:	0f 1f 40 00          	nopl   0x0(%rax)

0000000000001170 <frame_dummy>:
    1170:	f3 0f 1e fa          	endbr64 
    1174:	e9 67 ff ff ff       	jmpq   10e0 <register_tm_clones>
    1179:	0f 1f 80 00 00 00 00 	nopl   0x0(%rax)

0000000000001180 <__libc_csu_init>:
    1180:	f3 0f 1e fa          	endbr64 
    1184:	41 57                	push   %r15
    1186:	4c 8d 3d eb 2b 00 00 	lea    0x2beb(%rip),%r15        # 3d78 <__frame_dummy_init_array_entry>
    118d:	41 56                	push   %r14
    118f:	49 89 d6             	mov    %rdx,%r14
    1192:	41 55                	push   %r13
    1194:	49 89 f5             	mov    %rsi,%r13
    1197:	41 54                	push   %r12
    1199:	41 89 fc             	mov    %edi,%r12d
    119c:	55                   	push   %rbp
    119d:	48 8d 2d dc 2b 00 00 	lea    0x2bdc(%rip),%rbp        # 3d80 <__do_global_dtors_aux_fini_array_entry>
    11a4:	53                   	push   %rbx
    11a5:	4c 29 fd             	sub    %r15,%rbp
    11a8:	48 83 ec 08          	sub    $0x8,%rsp
    11ac:	e8 4f fe ff ff       	callq  1000 <_init>
    11b1:	48 c1 fd 03          	sar    $0x3,%rbp
    11b5:	74 1f                	je     11d6 <__libc_csu_init+0x56>
    11b7:	31 db                	xor    %ebx,%ebx
    11b9:	0f 1f 80 00 00 00 00 	nopl   0x0(%rax)
    11c0:	4c 89 f2             	mov    %r14,%rdx
    11c3:	4c 89 ee             	mov    %r13,%rsi
    11c6:	44 89 e7             	mov    %r12d,%edi
    11c9:	41 ff 14 df          	callq  *(%r15,%rbx,8)
    11cd:	48 83 c3 01          	add    $0x1,%rbx
    11d1:	48 39 dd             	cmp    %rbx,%rbp
    11d4:	75 ea                	jne    11c0 <__libc_csu_init+0x40>
    11d6:	48 83 c4 08          	add    $0x8,%rsp
    11da:	5b                   	pop    %rbx
    11db:	5d                   	pop    %rbp
    11dc:	41 5c                	pop    %r12
    11de:	41 5d                	pop    %r13
    11e0:	41 5e                	pop    %r14
    11e2:	41 5f                	pop    %r15
    11e4:	c3                   	retq   
    11e5:	66 66 2e 0f 1f 84 00 	data16 nopw %cs:0x0(%rax,%rax,1)
    11ec:	00 00 00 00 

00000000000011f0 <__libc_csu_fini>:
    11f0:	f3 0f 1e fa          	endbr64 
    11f4:	c3                   	retq   

Disassembly of section .fini:

00000000000011f8 <_fini>:
    11f8:	f3 0f 1e fa          	endbr64 
    11fc:	48 83 ec 08          	sub    $0x8,%rsp
    1200:	48 83 c4 08          	add    $0x8,%rsp
    1204:	c3                   	retq   
