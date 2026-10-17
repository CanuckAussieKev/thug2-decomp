package compiler

import (
	"encoding/binary"
	"fmt"
	"math"
	"strconv"
	"strings"
)

type CustomByteBuffer struct {
	Buffer []byte
}

type BytecodeCompiler struct {
	RootAstNode        AstNode
	Bytes              []byte
	NextLoopBypasserId int
	TargetGame         string
	RemoveChecksums    bool
}

func GenerateBytecode(compiler *BytecodeCompiler) {
	write := func(bytes ...byte) {
		compiler.Bytes = append(compiler.Bytes, bytes...)
	}

	writeIndex := func(index int, bytes ...byte) {
		i := index
		j := 0
		for {
			if i >= len(compiler.Bytes) || j >= len(bytes) {
				break
			}
			compiler.Bytes[i] = bytes[j]
			i++
			j++
		}
	}

	writeLittleUint32 := func(n uint32) {
		bytes := make([]byte, 4)
		binary.LittleEndian.PutUint32(bytes, n)
		write(bytes...)
	}

	writeLittleUint32Index := func(n uint32, index int) {
		bytes := make([]byte, 4)
		binary.LittleEndian.PutUint32(bytes, n)
		writeIndex(index, bytes...)
	}

	writeLittleUint16 := func(n uint16) {
		bytes := make([]byte, 2)
		binary.LittleEndian.PutUint16(bytes, n)
		write(bytes...)
	}

	writeLittleUint16Index := func(n uint16, index int) {
		bytes := make([]byte, 2)
		binary.LittleEndian.PutUint16(bytes, n)
		writeIndex(index, bytes...)
	}

	nameTable := make(map[string]uint32)

	// Explicit trailing-name-table order, populated by __register_checksums__
	// directives (which the decompiler emits to pin the table to the original's
	// order — THUG2's loader requires it). When non-empty this overrides the
	// map-iteration order below.
	var nameTableOrder []string
	nameTableOrderSeen := make(map[string]bool)

	var writeBytecodeForNode func(node AstNode)
	var writeBytecodeForIf func(node AstNode)
	var writeBytecodeForIfElse func(conditionNode AstNode, bodyNodes []AstNode, elseNodes []AstNode, hasElse bool)
	var writeBytecodeForBinaryExpression func(node AstNode, operator byte)
	var writeBytecodeForBinaryExpressionWithParentheses func(node AstNode, operator byte)
	var writeBytecodeForChecksum func(node AstNode)
	var writeBytecodeForPair func(node AstNode)
	var writeBytecodeForVector func(node AstNode)
	var writeBytecodeForInteger func(node AstNode)
	var writeBytecodeForFloat func(node AstNode)

	writeBytecodeForNode = func(node AstNode) {
		switch node.Kind {
		case AstKind_Root:
			for _, rootNode := range node.Data.(AstData_Root).BodyNodes {
				writeBytecodeForNode(rootNode)
			}
		case AstKind_NewLine:
			write(1)
		case AstKind_Comma:
			write(9)
		case AstKind_Break:
			write(0x22)
		case AstKind_AllArguments:
			write(0x2C)
		case AstKind_LocalReference:
			write(0x2D)
			writeBytecodeForNode(node.Data.(AstData_LocalReference).Node)
		case AstKind_Checksum:
			writeBytecodeForChecksum(node)
		case AstKind_Integer:
			writeBytecodeForInteger(node)
		case AstKind_Float:
			writeBytecodeForFloat(node)
		case AstKind_String:
			write(0x1B)
			stringData := node.Data.(AstData_String).StringToken.Data
			stringData = stringData[1 : len(stringData)-1]
			stringData = strings.Replace(stringData, "\\\\", "\\", -1)
			stringData = strings.Replace(stringData, "\\\"", "\"", -1)
			writeLittleUint32(uint32(len(stringData) + 1))
			write([]byte(stringData)...)
			write(0)
		case AstKind_LocalString:
			// THUG2 LocalString (0x1C): same payload as a String (0x1B) but a
			// distinct opcode. The `%"..."` sigil preserves the distinction.
			write(0x1C)
			stringData := node.Data.(AstData_String).StringToken.Data
			stringData = stringData[1 : len(stringData)-1]
			stringData = strings.Replace(stringData, "\\\\", "\\", -1)
			stringData = strings.Replace(stringData, "\\\"", "\"", -1)
			writeLittleUint32(uint32(len(stringData) + 1))
			write([]byte(stringData)...)
			write(0)
		case AstKind_Pair:
			writeBytecodeForPair(node)
		case AstKind_RandomRange:
			// 0x30 followed by a pair (0x1F + two floats)
			write(0x30)
			writeBytecodeForNode(node.Data.(AstData_UnaryExpression).Node)
		case AstKind_Vector:
			writeBytecodeForVector(node)
		case AstKind_UnaryExpression:
			write(0xE)
			writeBytecodeForNode(node.Data.(AstData_UnaryExpression).Node)
			write(0xF)
		case AstKind_SubtractionExpression:
			writeBytecodeForBinaryExpressionWithParentheses(node, 0xA)
		case AstKind_AdditionExpression:
			writeBytecodeForBinaryExpressionWithParentheses(node, 0xB)
		case AstKind_DivisionExpression:
			writeBytecodeForBinaryExpressionWithParentheses(node, 0xC)
		case AstKind_MultiplicationExpression:
			writeBytecodeForBinaryExpressionWithParentheses(node, 0xD)
		case AstKind_LessThanExpression:
			writeBytecodeForBinaryExpressionWithParentheses(node, 0x12)
		case AstKind_LessThanEqualsExpression:
			writeBytecodeForNode(AstNode{
				Kind: AstKind_LogicalNot,
				Data: AstData_UnaryExpression{
					Node: AstNode{
						Kind: AstKind_GreaterThanExpression,
						Data: node.Data,
					},
				},
			})
		case AstKind_GreaterThanExpression:
			writeBytecodeForBinaryExpressionWithParentheses(node, 0x14)
		case AstKind_GreaterThanEqualsExpression:
			writeBytecodeForNode(AstNode{
				Kind: AstKind_LogicalNot,
				Data: AstData_UnaryExpression{
					Node: AstNode{
						Kind: AstKind_LessThanExpression,
						Data: node.Data,
					},
				},
			})
		case AstKind_EqualsExpression:
			writeBytecodeForBinaryExpressionWithParentheses(node, 0x7)
		case AstKind_NotEqualExpression:
			writeBytecodeForNode(AstNode{
				Kind: AstKind_LogicalNot,
				Data: AstData_UnaryExpression{
					Node: AstNode{
						Kind: AstKind_EqualsExpression,
						Data: node.Data,
					},
				},
			})
		case AstKind_DotExpression:
			writeBytecodeForBinaryExpression(node, 0x8)
		case AstKind_ColonExpression:
			writeBytecodeForBinaryExpression(node, 0x42)
		case AstKind_LogicalNot:
			write(0x39)
			writeBytecodeForNode(node.Data.(AstData_UnaryExpression).Node)
		case AstKind_LogicalAnd:
			writeBytecodeForBinaryExpression(node, 0x33)
		case AstKind_LogicalOr:
			writeBytecodeForBinaryExpression(node, 0x32)
		case AstKind_FlatExpression:
			// A parenthesised expression with 2+ operators, e.g. (A = 0 or B = <c>).
			// THUG2 stores it as one flat infix stream inside a single 0xE/0xF pair,
			// with operator bytes inline and NO nested parentheses. Emit it verbatim.
			data := node.Data.(AstData_FlatExpression)
			write(0xE)
			for i := range data.Operands {
				// Operators[i-1] joins operand i-1 and i. When it is missing (i-1 >=
				// len(Operators)) the operands are ADJACENT with no operator byte,
				// which is how THUG2 encodes `(<x> -1)` (operand, signed-literal):
				// 0xE <x> <int -1> 0xF.
				if i > 0 && i-1 < len(data.Operators) {
					// Newlines stored before this operator (multi-line flat expr).
					if i-1 < len(data.NewlinesBeforeOperator) {
						for n := 0; n < data.NewlinesBeforeOperator[i-1]; n++ {
							write(1)
						}
					}
					operatorByte, _ := FlatOperatorByte(data.Operators[i-1])
					write(operatorByte)
					// Newlines stored after the operator, before this operand.
					if i-1 < len(data.NewlinesAfterOperator) {
						for n := 0; n < data.NewlinesAfterOperator[i-1]; n++ {
							write(1)
						}
					}
				}
				writeBytecodeForNode(data.Operands[i])
			}
			for n := 0; n < data.TrailingNewlines; n++ {
				write(1)
			}
			write(0xF)
		case AstKind_Comment:
			//writeBytecodeForNode(AstNode{
			//	Kind: AstKind_String,
			//	Data: AstData_String{
			//		StringToken: Token{
			//			Kind: TokenKind_String,
			//			Data: "\"" + node.Data.(AstData_Comment).CommentToken.Data + "\"",
			//		},
			//	},
			//})
		case AstKind_Script:
			data := node.Data.(AstData_Script)
			write(0x23)
			writeBytecodeForNode(data.NameNode)
			for _, defaultParameterNode := range data.DefaultParameterNodes {
				writeBytecodeForNode(defaultParameterNode)
			}
			for _, bodyNode := range data.BodyNodes {
				writeBytecodeForNode(bodyNode)
			}
			write(0x24)
		case AstKind_IfStatement:
			writeBytecodeForIf(node)
		case AstKind_Random:
			data := node.Data.(AstData_Random)

			numBranches := len(data.Branches)

			if data.IsRandom3 {
				write(0x41)
			} else if data.IsNoRepeat {
				write(0x40)
			} else {
				write(0x2F)
			}
			writeLittleUint32(uint32(numBranches))

			// write branch weights
			for i := 0; i < numBranches; i++ {
				branchWeightAsInt, _ := strconv.ParseInt(data.BranchWeights[i].Data.(AstData_Integer).IntegerToken.Data, 10, 32)
				writeLittleUint16(uint16(branchWeightAsInt))
			}

			branchOffsetsIndex := len(compiler.Bytes)

			// write dummy branch offsets (populate later)
			for i := 0; i < numBranches; i++ {
				var offset uint32 = 2
				writeLittleUint32(offset)
			}

			// Most randoms place a single newline (0x01) between the offset table
			// and the first branch, and every branch offset is measured to account
			// for it (+1 below). But it's per-random (formatting), so emit it only
			// when the source had it (a newline right after `{`); otherwise the
			// offsets must NOT include the +1.
			if data.Branch0Newline {
				write(0x01)
			}

			// write branches (record sizes for offset calculations, record longjump positions)
			branchSizes := make([]int, numBranches)
			longJumpPositions := make([]int, numBranches-1)
			for i := 0; i < numBranches; i++ {
				start := len(compiler.Bytes)
				for _, branchNode := range data.Branches[i] {
					writeBytecodeForNode(branchNode)
				}
				if i < (numBranches - 1) {
					// write dummy longjump offset (populate later)
					longJumpPositions[i] = len(compiler.Bytes)
					write(0x2e)
					writeLittleUint32(0)
				}
				end := len(compiler.Bytes)
				branchSizes[i] = end - start
			}

			finalIndex := len(compiler.Bytes)

			// update branch offsets with real values
			for i := 0; i < numBranches; i++ {
				offsetIndex := branchOffsetsIndex + (4 * i)

				offsetValue := 0
				if data.Branch0Newline {
					offsetValue = 1 // +1 for the 0x01 newline before the first branch
				}

				// include next branch offsets in offsetValue
				for j := i + 1; j < numBranches; j++ {
					offsetValue += 4
				}

				// include previous branch sizes in offsetValue too
				for j := 0; j < i; j++ {
					offsetValue += branchSizes[j]
				}

				writeLittleUint32Index(uint32(offsetValue), offsetIndex)
			}

			// update longjump offsets with real values. LongJumpDelta is normally 0
			// (jumps target the random's structural end); a few originals store a
			// non-canonical target that lands this many bytes further out.
			for i := 0; i < numBranches-1; i++ {
				realOffset := finalIndex + data.LongJumpDelta - longJumpPositions[i] - 5
				writeLittleUint32Index(uint32(realOffset), longJumpPositions[i]+1)
			}

		case AstKind_WhileLoop:
			// Emit a plain begin/repeat loop (0x20 ... 0x21), matching the
			// original Neversoft bytecode. (Previously THUG2 builds injected an
			// "infinite_loop_bypasser" guard, but it assigned a GLOBAL while the
			// guard read an undefined LOCAL — semantically wrong and not present
			// in real game scripts, which loop forever via an in-body `wait`.)
			write(0x20)
			for _, bodyNode := range node.Data.(AstData_WhileLoop).BodyNodes {
				writeBytecodeForNode(bodyNode)
			}
			write(0x21)
		case AstKind_RepeatLoop:
			// Counted loop: 0x00 (Begin) <body> 0x21 (Repeat) [<count>].
			repeatData := node.Data.(AstData_RepeatLoop)
			write(0x00)
			for _, bodyNode := range repeatData.BodyNodes {
				writeBytecodeForNode(bodyNode)
			}
			write(0x21)
			if repeatData.HasCount {
				writeBytecodeForNode(repeatData.CountNode)
			}
		case AstKind_Switch:
			// THUG2 native switch:
			//   0x3C value 0x01
			//   ( 0x3E 0x49<introOff> caseValue <body> 0x49<trailOff> )*
			//   ( 0x3F 0x49<defOff> <defaultBody> )?
			//   0x3D
			// 0x49 (short-break) offsets are little-endian uint16 measured FROM the
			// 0x49 opcode position. Layout the bytes first, recording each 0x49
			// position, then backpatch the offsets.
			switchData := node.Data.(AstData_Switch)

			write(0x3C)
			writeBytecodeForNode(switchData.ValueNode)
			// Newlines between the switch value and the first case (usually 1).
			switchValueNewlines := switchData.NewlinesAfterValue
			if switchValueNewlines < 1 {
				switchValueNewlines = 1
			}
			for n := 0; n < switchValueNewlines; n++ {
				write(0x01)
			}

			introPositions := make([]int, len(switchData.CaseValues))
			trailPositions := make([]int, len(switchData.CaseValues))
			for i := range switchData.CaseValues {
				write(0x3E)
				introPositions[i] = len(compiler.Bytes)
				write(0x49, 0x00, 0x00)
				writeBytecodeForNode(switchData.CaseValues[i])
				for _, bodyNode := range switchData.CaseBodies[i] {
					writeBytecodeForNode(bodyNode)
				}
				// The construct physically abutting `endswitch` carries no
				// trailing short-break (its `break` would be a redundant
				// fall-through). When there is no default, that construct is the
				// final case, so the original Neversoft compiler elides its
				// trailing 0x49. (When a default exists, every case still needs
				// its break to skip over the default body, and the default —
				// emitted below — is the one abutting endswitch.)
				isFinalCaseAbuttingEndswitch := i == len(switchData.CaseValues)-1 && !switchData.HasDefault
				if isFinalCaseAbuttingEndswitch {
					trailPositions[i] = -1
				} else {
					trailPositions[i] = len(compiler.Bytes)
					write(0x49, 0x00, 0x00)
				}
			}

			defaultPosition := -1
			if switchData.HasDefault {
				write(0x3F)
				defaultPosition = len(compiler.Bytes)
				write(0x49, 0x00, 0x00)
				for _, bodyNode := range switchData.DefaultBody {
					writeBytecodeForNode(bodyNode)
				}
			}

			endswitchPosition := len(compiler.Bytes)
			write(0x3D)

			// Backpatch short-break offsets. Every intro short-break targets the
			// byte just before the next construct (case/default/endswitch):
			//   introOff = (trailPos + 2) - introPos   (targets the trail SB's last offset byte,
			//                                            i.e. one before the next case/default)
			//   trailOff = endswitchPos - trailPos
			//   defOff   = (endswitchPos - 1) - defPos (targets the byte before endswitch)
			// A final case with no default has no trailing SB (trailPos == -1); its
			// intro instead targets endswitchPos-1, exactly like a default.
			for i := range switchData.CaseValues {
				if trailPositions[i] == -1 {
					introOff := (endswitchPosition - 1) - introPositions[i]
					writeLittleUint16Index(uint16(introOff), introPositions[i]+1)
					continue
				}
				introOff := (trailPositions[i] + 2) - introPositions[i]
				writeLittleUint16Index(uint16(introOff), introPositions[i]+1)
				trailOff := endswitchPosition - trailPositions[i]
				writeLittleUint16Index(uint16(trailOff), trailPositions[i]+1)
			}
			if switchData.HasDefault {
				defOff := (endswitchPosition - 1) - defaultPosition
				writeLittleUint16Index(uint16(defOff), defaultPosition+1)
			}
		case AstKind_Return:
			data := node.Data.(AstData_UnaryExpression)

			var invocationData AstData_Invocation
			if data.Node.Kind == AstKind_Checksum {
				invocationData = AstData_Invocation{
					ScriptIdentifierNode:              data.Node,
					ParameterNodes:                    []AstNode{},
					TokensConsumedByEachParameterNode: []int{},
				}
			} else {
				invocationData = data.Node.Data.(AstData_Invocation)
			}

			write(0x29)
			for _, parameterNode := range invocationData.ParameterNodes {

				// Replace 'true' and 'false' with __boolean_result__ parameter
				if parameterNode.Kind == AstKind_Checksum {
					checksumData := parameterNode.Data.(AstData_Checksum)
					name := checksumData.ChecksumToken.Data
					booleanParameterNode := func(integerToken string) AstNode {
						return AstNode{
							Kind: AstKind_Assignment,
							Data: AstData_Assignment{
								NameNode: AstNode{
									Kind: AstKind_Checksum,
									Data: AstData_Checksum{
										IsRawChecksum: false,
										ChecksumToken: Token{
											Kind: TokenKind_Identifier,
											Data: "__boolean_result__",
										},
									},
								},
								ValueNode: AstNode{
									Kind: AstKind_Integer,
									Data: AstData_Integer{
										IntegerToken: Token{
											Kind: TokenKind_Integer,
											Data: integerToken,
										},
									},
								},
							},
						}
					}
					if name == "true" {
						parameterNode = booleanParameterNode("1")
					} else if name == "false" {
						parameterNode = booleanParameterNode("0")
					}
				}
				writeBytecodeForNode(parameterNode)
			}
		case AstKind_Invocation:
			data := node.Data.(AstData_Invocation)
			writeBytecodeForNode(data.ScriptIdentifierNode)
			for _, parameterNode := range data.ParameterNodes {
				writeBytecodeForNode(parameterNode)
			}
		case AstKind_NameTableEntry:
			// Checksum names declared via __register_checksums__: register them so
			// they appear in the trailing name table, IN THIS ORDER, and emit NO
			// body bytecode. A name may carry an explicit non-canonical checksum
			// override (Hashes[i] >= 0) to reproduce a quirky original table hash.
			entryData := node.Data.(AstData_NameTableEntry)
			for i, name := range entryData.Names {
				checksum := StringToChecksum(name)
				if i < len(entryData.Hashes) && entryData.Hashes[i] >= 0 {
					checksum = uint32(entryData.Hashes[i])
				}
				nameTable[name] = checksum
				if !nameTableOrderSeen[name] {
					nameTableOrderSeen[name] = true
					nameTableOrder = append(nameTableOrder, name)
				}
			}
		case AstKind_Assignment:
			data := node.Data.(AstData_Assignment)
			writeBytecodeForNode(data.NameNode)
			// Re-emit newlines that sat between the name and '=' (`name\n= value`),
			// stored as 0x01 bytes before the 0x07.
			for i := 0; i < data.NewlinesBeforeEquals; i++ {
				write(1)
			}
			write(7)
			// Re-emit newlines that sat between '=' and the value (e.g. a struct on
			// the next line), which THUG2 stores as 0x01 bytes after the 0x07.
			for i := 0; i < data.NewlinesAfterEquals; i++ {
				write(1)
			}
			writeBytecodeForNode(data.ValueNode)
		case AstKind_Struct:
			write(3)
			for _, elementNode := range node.Data.(AstData_Struct).ElementNodes {
				writeBytecodeForNode(elementNode)
			}
			write(4)
		case AstKind_Array:
			write(5)
			for _, elementNode := range node.Data.(AstData_Array).ElementNodes {
				writeBytecodeForNode(elementNode)
			}
			write(6)
		case AstKind_ArrayAccess:
			data := node.Data.(AstData_ArrayAccess)
			writeBytecodeForNode(data.Array)
			write(5)
			writeBytecodeForNode(data.Index)
			write(6)
		default:
			fmt.Printf("Warning: no bytecode generated for AstNode of type '%s'\n", node.Kind.String())
		}
	}

	var transformIfElseIf func(ifElseIfNode AstNode) AstNode
	transformIfElseIf = func(ifElseIfNode AstNode) AstNode {
		ifElseIfData := ifElseIfNode.Data.(AstData_IfStatement)

		if len(ifElseIfData.Conditions) == 1 {
			return ifElseIfNode
		}

		bodies := [][]AstNode{
			ifElseIfData.Bodies[0],
		}

		if len(ifElseIfData.Conditions) > 1 {
			bodies = append(bodies,
				[]AstNode{
					{
						Kind: AstKind_NewLine,
						Data: AstData_Empty{},
					},
					transformIfElseIf(AstNode{
						Kind: AstKind_IfStatement,
						Data: AstData_IfStatement{
							Conditions: ifElseIfData.Conditions[1:],
							Bodies:     ifElseIfData.Bodies[1:],
						},
					}),
					{
						Kind: AstKind_NewLine,
						Data: AstData_Empty{},
					},
				},
			)
		}

		return AstNode{
			Kind: AstKind_IfStatement,
			Data: AstData_IfStatement{
				Conditions: []AstNode{
					ifElseIfData.Conditions[0],
				},
				Bodies: bodies,
			},
		}
	}

	writeBytecodeForIf = func(node AstNode) {
		transformedIf := transformIfElseIf(node)
		transformedIfData := transformedIf.Data.(AstData_IfStatement)
		hasElse := len(transformedIfData.Bodies) > 1
		var elseNodes []AstNode
		if hasElse {
			elseNodes = transformedIfData.Bodies[1]
		}

		writeBytecodeForIfElse(
			transformedIfData.Conditions[0],
			transformedIfData.Bodies[0],
			elseNodes,
			hasElse,
		)
	}

	writeBytecodeForBinaryExpressionWithParentheses = func(node AstNode, operator byte) {
		write(0xE)
		writeBytecodeForBinaryExpression(node, operator)
		write(0xF)
	}

	writeBytecodeForBinaryExpression = func(node AstNode, operator byte) {
		data := node.Data.(AstData_BinaryExpression)
		writeBytecodeForNode(data.LeftNode)
		write(operator)
		writeBytecodeForNode(data.RightNode)
	}

	writeBytecodeForChecksum = func(node AstNode) {
		write(0x16)
		data := node.Data.(AstData_Checksum)

		var checksum uint32
		if data.IsRawChecksum {
			temp1 := data.ChecksumToken.Data[1:]
			temp1 = temp1[6:8] + temp1[4:6] + temp1[2:4] + temp1[0:2]

			temp2, _ := strconv.ParseUint(temp1, 16, 32)
			checksum = uint32(temp2)
		} else {
			name := data.ChecksumToken.Data
			checksum = StringToChecksum(name)
			nameTable[name] = checksum
		}

		writeLittleUint32(checksum)
	}

	writeBytecodeForInteger = func(node AstNode) {
		write(0x17)
		intValue, _ := strconv.ParseInt(node.Data.(AstData_Integer).IntegerToken.Data, 10, 32)
		writeLittleUint32(uint32(intValue))
	}

	writeBytecodeForFloat = func(node AstNode) {
		write(0x1A)
		floatValue, _ := strconv.ParseFloat(node.Data.(AstData_Float).FloatToken.Data, 32)
		var bytesValue [4]byte
		binary.LittleEndian.PutUint32(bytesValue[:], math.Float32bits(float32(floatValue)))
		write(bytesValue[:]...)
	}

	writeBytecodeForPair = func(node AstNode) {
		write(0x1F)
		floatValueA, _ := strconv.ParseFloat(node.Data.(AstData_Pair).FloatNodeA.Data.(AstData_Float).FloatToken.Data, 32)
		var bytesValueA [4]byte
		binary.LittleEndian.PutUint32(bytesValueA[:], math.Float32bits(float32(floatValueA)))
		write(bytesValueA[:]...)
		floatValueB, _ := strconv.ParseFloat(node.Data.(AstData_Pair).FloatNodeB.Data.(AstData_Float).FloatToken.Data, 32)
		var bytesValueB [4]byte
		binary.LittleEndian.PutUint32(bytesValueB[:], math.Float32bits(float32(floatValueB)))
		write(bytesValueB[:]...)
	}

	writeBytecodeForVector = func(node AstNode) {
		write(0x1E)
		floatValueA, _ := strconv.ParseFloat(node.Data.(AstData_Vector).FloatNodeA.Data.(AstData_Float).FloatToken.Data, 32)
		var bytesValueA [4]byte
		binary.LittleEndian.PutUint32(bytesValueA[:], math.Float32bits(float32(floatValueA)))
		write(bytesValueA[:]...)
		floatValueB, _ := strconv.ParseFloat(node.Data.(AstData_Vector).FloatNodeB.Data.(AstData_Float).FloatToken.Data, 32)
		var bytesValueB [4]byte
		binary.LittleEndian.PutUint32(bytesValueB[:], math.Float32bits(float32(floatValueB)))
		write(bytesValueB[:]...)
		floatValueC, _ := strconv.ParseFloat(node.Data.(AstData_Vector).FloatNodeC.Data.(AstData_Float).FloatToken.Data, 32)
		var bytesValueC [4]byte
		binary.LittleEndian.PutUint32(bytesValueC[:], math.Float32bits(float32(floatValueC)))
		write(bytesValueC[:]...)
	}

	writeBytecodeForIfElse = func(conditionNode AstNode, bodyNodes []AstNode, elseNodes []AstNode, hasElse bool) {
		{
			updatedConditionNode := conditionNode
			conditionStart := len(compiler.Bytes)

			if compiler.TargetGame == "thug2" {
				write(0x47)
				write(0x00) // 2 temporary bytes for branch size
				write(0x00)
			} else {
				write(0x25) // use If1 instead of If2
			}

			writeBytecodeForNode(updatedConditionNode)
			for _, bodyNode := range bodyNodes {
				writeBytecodeForNode(bodyNode)
			}
			end := len(compiler.Bytes)
			size := end - conditionStart
			if hasElse {
				size += 2
			}
			if compiler.TargetGame == "thug2" {
				writeLittleUint16Index(uint16(size), conditionStart+1)
			}
		}
		if hasElse {
			start := len(compiler.Bytes)
			if compiler.TargetGame == "thug2" {
				write(0x48)
				write(0x00) // 2 temporary bytes for branch size
				write(0x00)
			} else {
				write(0x26) // use Else1 instead of Else2
			}
			for _, bodyNode := range elseNodes {
				writeBytecodeForNode(bodyNode)
			}
			end := len(compiler.Bytes)
			if compiler.TargetGame == "thug2" {
				writeLittleUint16Index(uint16(end-start), start+1)
			}
		}
		write(0x28)
	}

	writeNameTableEntry := func(checksum uint32, name string) {
		write(0x2B)
		writeLittleUint32(checksum)
		write([]byte(name)...)
		write(0)
	}

	// -----------

	writeBytecodeForNode(compiler.RootAstNode)

	if !compiler.RemoveChecksums {
		if len(nameTableOrder) > 0 {
			// Emit in the explicit order from __register_checksums__ (original
			// table order, which THUG2 requires). Append any names referenced in
			// the body but somehow not declared, so nothing is dropped.
			for _, name := range nameTableOrder {
				writeNameTableEntry(nameTable[name], name)
			}
			for name, checksum := range nameTable {
				if !nameTableOrderSeen[name] {
					writeNameTableEntry(checksum, name)
				}
			}
		} else {
			for name, checksum := range nameTable {
				writeNameTableEntry(checksum, name)
			}
		}
	}
	write(0)
}

// FlatOperatorByte maps an operator AstKind to its single THUG2 bytecode byte,
// for use inside a flat parenthesised expression (AstKind_FlatExpression). The
// boolean is false for operators that have no direct single-byte form here
// (e.g. !=, which the compiler only emits via negation); the parser refuses to
// build a flat node in that case, falling back to existing behaviour.
func FlatOperatorByte(kind AstKind) (byte, bool) {
	switch kind {
	case AstKind_EqualsExpression:
		return 0x7, true
	case AstKind_LessThanExpression:
		return 0x12, true
	case AstKind_LessThanEqualsExpression:
		return 0x13, true
	case AstKind_GreaterThanExpression:
		return 0x14, true
	case AstKind_GreaterThanEqualsExpression:
		return 0x15, true
	case AstKind_AdditionExpression:
		return 0xB, true
	case AstKind_SubtractionExpression:
		return 0xA, true
	case AstKind_MultiplicationExpression:
		return 0xD, true
	case AstKind_DivisionExpression:
		return 0xC, true
	case AstKind_LogicalOr:
		return 0x32, true
	case AstKind_LogicalAnd:
		return 0x33, true
	}
	return 0, false
}
