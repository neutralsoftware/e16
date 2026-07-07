if exists("b:current_syntax")
  finish
endif

syntax case match

syntax match e16Comment /;.*/
syntax region e16String start=/"/ skip=/\\"/ end=/"/
syntax region e16String start=/'/ skip=/\\'/ end=/'/
syntax match e16Number /\v<[-+]?(0x[0-9A-Fa-f]+|0b[01]+|0o[0-7]+|[0-9]+)>/
syntax match e16Directive /\v\.(const|constant|string|data|byte|word|addr24)>/
syntax match e16ConstantDef /\v^\s*\.(const|constant)\s+\zs[A-Za-z_.][A-Za-z0-9_.]*/
syntax match e16LabelDef /\v^\s*\zs[A-Za-z_.][A-Za-z0-9_.]*\ze:/
syntax match e16Operator /[@#(),:+-]/

syntax keyword e16Instruction nop mov movb movw clr swap xchg
syntax keyword e16Instruction load loadb loadw loadsb store storeb storew addr
syntax keyword e16Instruction add addwc sub subwc inc dec neg mul muls div divs mod
syntax keyword e16Instruction and or xor not test setb clearb toggleb
syntax keyword e16Instruction shl shr sar rol ror rcl rcr
syntax keyword e16Instruction cmp
syntax keyword e16Instruction jmp bra beq bne bcs bcc bmi bpl bvs bvc bgt bge blt ble bhi bls
syntax keyword e16Instruction call ret enter leave push pop pushf popf pusha popa
syntax keyword e16Instruction int iret ei di wait halt reset trap
syntax keyword e16Instruction get set dma

syntax keyword e16Register r0 r1 r2 r3 r4 r5 r6 r7 r8 r9 r10 r11 r12 r13 r14 r15
syntax keyword e16Register pc sp fp fl dp ivt

highlight default link e16Comment Comment
highlight default link e16String String
highlight default link e16Number Number
highlight default link e16Directive PreProc
highlight default link e16ConstantDef Constant
highlight default link e16LabelDef Function
highlight default link e16Operator Operator
highlight default link e16Instruction Keyword
highlight default link e16Register Identifier

let b:current_syntax = "e16"
