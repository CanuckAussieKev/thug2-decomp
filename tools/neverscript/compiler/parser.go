package compiler

import (
	"errors"
	"fmt"
	"os"
	"strconv"
	"strings"
)

var ptrace = os.Getenv("NS_PTRACE") != ""

type ParseResult struct {
	GotResult      bool
	Error          error
	Reason         string
	Node           AstNode
	TokensConsumed int
	LineNumber     int
}

type Parser struct {
	Tokens []Token
	Result ParseResult
}

func BuildAbstractSyntaxTree(parser *Parser) {
	var ParseRoot func( /*index is always 0*/ ) ParseResult
	var ParseRootBodyNode func(index int) ParseResult
	var ParseExpression func(index int, allowInvocations bool) ParseResult
	var ParseExpressionBeginningWithLeftParenthesis func(index int) ParseResult
	var ParseAssignment func(index int, allowInvocations bool) ParseResult
	var ParseScript func(index int) ParseResult
	var ParseBodyOfCode func(index int) (ParseResult, []AstNode)
	var ParseWhileLoop func(index int) ParseResult
	var ParseRepeatLoop func(index int) ParseResult
	var ParseSwitch func(index int) ParseResult
	var ParseLogicalNot func(index int) ParseResult
	var ParseIfStatement func(index int) ParseResult
	var ParseChecksumOrInvocation func(index int, allowInvocations bool) ParseResult
	var ParseInvocation func(index int) ParseResult
	var ParseInvocationParameter func(index int) ParseResult
	var ParseRandom func(index int) ParseResult
	var ParseComment func(index int) ParseResult
	var ParseChecksum func(index int) ParseResult
	var ParseFloat func(index int) ParseResult
	var ParseInteger func(index int) ParseResult
	var ParseString func(index int) ParseResult
	var ParseArray func(index int) ParseResult
	var ParseStruct func(index int) ParseResult
	var ParseNewLine func(index int) ParseResult
	var ParseRegisterChecksums func(index int) ParseResult
	var ParseBreak func(index int) ParseResult
	var ParseReturn func(index int) ParseResult
	var ParseComma func(index int) ParseResult
	// TODO(brandon): var SkipOverCommentsAndEscapedNewlines func(index int) int
	var GetKind func(index int) TokenKind
	var GetToken func(index int) Token

	// __register_checksums__ <name> <name> ...
	// Registers orphan symbol names (declared in the original .qb's trailing name
	// table but never referenced in code) so they survive recompilation. Emits no
	// body bytecode — the names just feed the trailing name table.
	ParseRegisterChecksums = func(index int) ParseResult {
		if GetKind(index) != TokenKind_Identifier || GetToken(index).Data != "__register_checksums__" {
			return ParseResult{GotResult: false}
		}
		start := index
		index++
		var names []string
		var hashes []int64
		// A name may be immediately followed by a `#rawhash` token: an explicit
		// checksum override for a name whose stored table hash is non-canonical
		// (differs from StringToChecksum(name)). Record it so the trailing name
		// table reproduces the exact original bytes; otherwise -1 = compute.
		appendName := func(n string) {
			names = append(names, n)
			if GetKind(index) == TokenKind_RawChecksum {
				hex := GetToken(index).Data[1:] // strip '#'; bytes are little-endian
				le := hex[6:8] + hex[4:6] + hex[2:4] + hex[0:2]
				v, _ := strconv.ParseUint(le, 16, 32)
				hashes = append(hashes, int64(v))
				index++
			} else {
				hashes = append(hashes, -1)
			}
		}
		for {
			if GetKind(index) == TokenKind_Identifier {
				name := GetToken(index).Data
				index++
				appendName(name)
			} else if GetKind(index) == TokenKind_String {
				// Non-identifier names (texture paths, names with spaces) are emitted
				// as quoted strings; strip the quotes and unescape \\ and \".
				raw := GetToken(index).Data
				raw = raw[1 : len(raw)-1]
				raw = strings.ReplaceAll(raw, "\\\"", "\"")
				raw = strings.ReplaceAll(raw, "\\\\", "\\")
				index++
				appendName(raw)
			} else {
				break
			}
		}
		return ParseResult{
			GotResult:      true,
			Node:           AstNode{Kind: AstKind_NameTableEntry, Data: AstData_NameTableEntry{Names: names, Hashes: hashes}},
			TokensConsumed: index - start,
		}
	}

	ParseRoot = func() ParseResult {
		var bodyNodes AstNodeBuffer

		// (Original Neversoft .qb files do not begin with a leading newline, so
		// we don't inject one — it would shift the whole file by a byte.)

		// Parse root body nodes until you can't anymore
		index := 0
		var bodyNodeParseResult ParseResult
		for {
			bodyNodeParseResult = ParseRootBodyNode(index)

			if bodyNodeParseResult.GotResult {
				if bodyNodeParseResult.Error != nil {
					return bodyNodeParseResult
				}
				bodyNodes.MaybeSave(bodyNodeParseResult)
				index += bodyNodeParseResult.TokensConsumed
			} else {
				break
			}
		}

		// Display
		if numOfTokens := len(parser.Tokens); bodyNodes.TokensConsumed < numOfTokens {
			var messageBuilder strings.Builder
			messageBuilder.WriteString("\n\nFinished parsing but didn't read all tokens.\n")
			messageBuilder.WriteString(fmt.Sprintf("Read %d/%d (%d left unread).\n", bodyNodes.TokensConsumed, numOfTokens, numOfTokens-bodyNodes.TokensConsumed))
			for _, unreadToken := range parser.Tokens[bodyNodes.TokensConsumed:numOfTokens] {
				messageBuilder.WriteString(fmt.Sprintf("  %+v,\n", unreadToken))
			}
			messageBuilder.WriteString(fmt.Sprintf("\nPotential cause: %s\n", bodyNodeParseResult.Reason))
			return ParseResult{
				GotResult: false,
				Reason:    messageBuilder.String(),
			}
		}

		return ParseResult{
			GotResult: true,
			Node: AstNode{
				Kind: AstKind_Root,
				Data: AstData_Root{
					BodyNodes: bodyNodes.Nodes,
				},
			},
			TokensConsumed: bodyNodes.TokensConsumed,
		}
	}

	ParseRootBodyNode = func(index int) ParseResult {
		if GetKind(index) == TokenKind_RightParenthesis {
			return ParseResult{
				GotResult:  true,
				Error:      errors.New("Unnecessary parenthesis )"),
				LineNumber: GetToken(index).LineNumber,
			}
		}

		if parseResult := ParseNewLine(index); parseResult.GotResult {
			return parseResult
		}

		if parseResult := ParseComment(index); parseResult.GotResult {
			return parseResult
		}

		if parseResult := ParseRegisterChecksums(index); parseResult.GotResult {
			return parseResult
		}

		if parseResult := ParseScript(index); parseResult.GotResult {
			return parseResult
		}

		if parseResult := ParseAssignment(index, true); parseResult.GotResult {
			return parseResult
		}

		if parseResult := ParseExpression(index, true); parseResult.GotResult {
			return parseResult
		}

		return ParseResult{
			GotResult: false,
			//Reason:    TokensNotRecognisedError(parser.Tokens[index:], "a root body node"),
		}
	}

	ParseExpression = func(index int, allowInvocations bool) ParseResult {
		parseExpressionInner := func(index int, allowInvocations bool) ParseResult {
			if GetKind(index) == TokenKind_If {
				return ParseResult{
					GotResult: false,
					Reason:    "Failed to parse expression, found 'if'",
				}
			}
			if GetKind(index) == TokenKind_LeftAngleBracket &&
				GetKind(index+1) == TokenKind_Dot &&
				GetKind(index+2) == TokenKind_Dot &&
				GetKind(index+3) == TokenKind_Dot &&
				GetKind(index+4) == TokenKind_RightAngleBracket {
				return ParseResult{
					GotResult: true,
					Node: AstNode{
						Kind: AstKind_AllArguments,
						Data: AstData_Empty{},
					},
					TokensConsumed: 5,
				}
			}
			if GetKind(index) == TokenKind_LeftAngleBracket {
				futureIndex := index + 1
				// parseResult := ParseChecksumOrInvocation(futureIndex, allowInvocations)
				// BUG: ^ invocation comes after the local reference, not inside
				parseResult := ParseChecksum(futureIndex)
				if !parseResult.GotResult {
					return ParseResult{
						GotResult: false,
						Reason:    WrapStr("Failed to parse expression for local reference", parseResult.Reason),
					}
				}
				futureIndex++
				if GetKind(futureIndex) != TokenKind_RightAngleBracket {
					return ParseResult{
						GotResult: false,
						Reason:    WrapStr("Failed to parse local reference, no '>'", parseResult.Reason),
					}
				}
				// A local invoked as a script: `<LoadFunction> <args>`. If an argument
				// plausibly follows the local reference, parse the whole thing as an
				// invocation (local as the script name). Use a cheap token-kind probe
				// rather than a speculative ParseExpression — the latter double-parses
				// every call (ParseInvocation re-parses) and is exponential on files
				// full of local invocations (e.g. allanims). The minus guard mirrors
				// the checksum case: `<x> - 1` is subtraction, not a call. `{` is left
				// out so an `if <x> { body }` body isn't swallowed as a struct arg.
				localConsumed := 2 + parseResult.TokensConsumed
				if allowInvocations {
					switch GetKind(index + localConsumed) {
					case TokenKind_Identifier, TokenKind_RawChecksum, TokenKind_LeftAngleBracket,
						TokenKind_Integer, TokenKind_Float, TokenKind_String, TokenKind_LocalString,
						TokenKind_LeftParenthesis, TokenKind_LeftSquareBracket:
						return ParseInvocation(index)
					}
				}
				return ParseResult{
					GotResult: true,
					Node: AstNode{
						Kind: AstKind_LocalReference,
						Data: AstData_LocalReference{
							Node: parseResult.Node,
						},
					},
					TokensConsumed: 2 + parseResult.TokensConsumed,
				}
			}
			if GetKind(index) == TokenKind_Minus {
				nextToken := GetToken(index + 1)
				if nextToken.Kind == TokenKind_Integer {
					return ParseResult{
						GotResult: true,
						Node: AstNode{
							Kind: AstKind_Integer,
							Data: AstData_Integer{
								IntegerToken: Token{
									Kind:       TokenKind_Integer,
									Data:       "-" + nextToken.Data,
									LineNumber: nextToken.LineNumber,
								},
							},
						},
						TokensConsumed: 2,
					}
				} else if nextToken.Kind == TokenKind_Float {
					return ParseResult{
						GotResult: true,
						Node: AstNode{
							Kind: AstKind_Float,
							Data: AstData_Float{
								FloatToken: Token{
									Kind:       TokenKind_Float,
									Data:       "-" + nextToken.Data,
									LineNumber: nextToken.LineNumber,
								},
							},
						},
						TokensConsumed: 2,
					}
				}
			}

			if parseResult := ParseRandom(index); parseResult.GotResult {
				return parseResult
			}
			if GetKind(index) == TokenKind_RandomRange {
				// randomrange (a, b) -> opcode 0x30 followed by a pair
				pairResult := ParseExpression(index+1, false)
				if !pairResult.GotResult {
					return ParseResult{GotResult: false, Reason: "randomrange not followed by a (float, float) pair"}
				}
				return ParseResult{
					GotResult:      true,
					Node:           AstNode{Kind: AstKind_RandomRange, Data: AstData_UnaryExpression{Node: pairResult.Node}},
					TokensConsumed: 1 + pairResult.TokensConsumed,
				}
			}
			if GetKind(index) == TokenKind_Identifier || GetKind(index) == TokenKind_RawChecksum {
				return ParseChecksumOrInvocation(index, allowInvocations)
			}
			if GetKind(index) == TokenKind_Bang {
				return ParseLogicalNot(index)
			}
			if GetKind(index) == TokenKind_Integer {
				return ParseInteger(index)
			}
			if GetKind(index) == TokenKind_Float {
				return ParseFloat(index)
			}
			if GetKind(index) == TokenKind_String {
				return ParseString(index)
			}
			if GetKind(index) == TokenKind_LocalString {
				return ParseResult{
					GotResult: true,
					Node: AstNode{
						Kind: AstKind_LocalString,
						Data: AstData_String{
							StringToken: GetToken(index),
						},
					},
					TokensConsumed: 1,
				}
			}
			if GetKind(index) == TokenKind_LeftParenthesis {
				return ParseExpressionBeginningWithLeftParenthesis(index)
			}
			if GetKind(index) == TokenKind_LeftSquareBracket {
				return ParseArray(index)
			}
			if GetKind(index) == TokenKind_LeftCurlyBrace {
				return ParseStruct(index)
			}
			return ParseResult{
				GotResult: false,
				//Reason:    TokensNotRecognisedError(parser.Tokens[index:], "an expression"),
				LineNumber: GetToken(index - 1).LineNumber,
			}
		}

		inPlaceMathOperationParseResult := func(index int, leftParseResult ParseResult, rightParseResult ParseResult, expressionKind AstKind) ParseResult {
			return ParseResult{
				GotResult: true,
				Node: AstNode{
					Kind: AstKind_Assignment,
					Data: AstData_Assignment{
						NameNode: leftParseResult.Node,
						ValueNode: AstNode{
							Kind: expressionKind,
							Data: AstData_BinaryExpression{
								LeftNode:  leftParseResult.Node,
								RightNode: rightParseResult.Node,
							},
						},
					},
				},
				TokensConsumed: leftParseResult.TokensConsumed + 2 + rightParseResult.TokensConsumed,
			}
		}

		expressionParseResult := parseExpressionInner(index, allowInvocations)
		if expressionParseResult.GotResult {
			if expressionParseResult.Error != nil {
				return expressionParseResult
			}
			index += expressionParseResult.TokensConsumed
			if GetKind(index) == TokenKind_Dot {
				index++
				secondExpressionParseResult := ParseExpression(index, true)
				if secondExpressionParseResult.GotResult {
					index += secondExpressionParseResult.TokensConsumed
					return ParseResult{
						GotResult: true,
						Node: AstNode{
							Kind: AstKind_DotExpression,
							Data: AstData_BinaryExpression{
								LeftNode:  expressionParseResult.Node,
								RightNode: secondExpressionParseResult.Node,
							},
						},
						TokensConsumed: expressionParseResult.TokensConsumed + 1 + secondExpressionParseResult.TokensConsumed,
					}
				}
			} else if GetKind(index) == TokenKind_Colon {
				index++
				secondExpressionParseResult := ParseExpression(index, true)
				if secondExpressionParseResult.GotResult {
					index += secondExpressionParseResult.TokensConsumed
					return ParseResult{
						GotResult: true,
						Node: AstNode{
							Kind: AstKind_ColonExpression,
							Data: AstData_BinaryExpression{
								LeftNode:  expressionParseResult.Node,
								RightNode: secondExpressionParseResult.Node,
							},
						},
						TokensConsumed: expressionParseResult.TokensConsumed + 1 + secondExpressionParseResult.TokensConsumed,
					}
				}
			} else if GetKind(index) == TokenKind_And {
				index++
				secondExpressionParseResult := ParseExpression(index, allowInvocations)
				if secondExpressionParseResult.GotResult {
					index += secondExpressionParseResult.TokensConsumed
					return ParseResult{
						GotResult: true,
						Node: AstNode{
							Kind: AstKind_LogicalAnd,
							Data: AstData_BinaryExpression{
								LeftNode:  expressionParseResult.Node,
								RightNode: secondExpressionParseResult.Node,
							},
						},
						TokensConsumed: expressionParseResult.TokensConsumed + 1 + secondExpressionParseResult.TokensConsumed,
					}
				}
			} else if GetKind(index) == TokenKind_Or {
				index++
				secondExpressionParseResult := ParseExpression(index, allowInvocations)
				if secondExpressionParseResult.GotResult {
					index += secondExpressionParseResult.TokensConsumed
					return ParseResult{
						GotResult: true,
						Node: AstNode{
							Kind: AstKind_LogicalOr,
							Data: AstData_BinaryExpression{
								LeftNode:  expressionParseResult.Node,
								RightNode: secondExpressionParseResult.Node,
							},
						},
						TokensConsumed: expressionParseResult.TokensConsumed + 1 + secondExpressionParseResult.TokensConsumed,
					}
				}
			} else if GetKind(index) == TokenKind_Plus && GetKind(index+1) == TokenKind_Equals {
				index += 2
				secondExpressionParseResult := ParseExpression(index, false)
				if secondExpressionParseResult.GotResult {
					index += secondExpressionParseResult.TokensConsumed
					return inPlaceMathOperationParseResult(index, expressionParseResult, secondExpressionParseResult, AstKind_AdditionExpression)
				} else {
					return ParseResult{
						GotResult:  true,
						Error:      errors.New("Incomplete +="),
						LineNumber: secondExpressionParseResult.LineNumber,
					}
				}
			} else if GetKind(index) == TokenKind_Minus && GetKind(index+1) == TokenKind_Equals {
				index += 2
				secondExpressionParseResult := ParseExpression(index, false)
				if secondExpressionParseResult.GotResult {
					index += secondExpressionParseResult.TokensConsumed
					return inPlaceMathOperationParseResult(index, expressionParseResult, secondExpressionParseResult, AstKind_SubtractionExpression)
				} else {
					return ParseResult{
						GotResult:  true,
						Error:      errors.New("Incomplete -="),
						LineNumber: secondExpressionParseResult.LineNumber,
					}
				}
			} else if GetKind(index) == TokenKind_Asterisk && GetKind(index+1) == TokenKind_Equals {
				index += 2
				secondExpressionParseResult := ParseExpression(index, false)
				if secondExpressionParseResult.GotResult {
					index += secondExpressionParseResult.TokensConsumed
					return inPlaceMathOperationParseResult(index, expressionParseResult, secondExpressionParseResult, AstKind_MultiplicationExpression)
				} else {
					return ParseResult{
						GotResult:  true,
						Error:      errors.New("Incomplete *="),
						LineNumber: secondExpressionParseResult.LineNumber,
					}
				}
			} else if GetKind(index) == TokenKind_ForwardSlash && GetKind(index+1) == TokenKind_Equals {
				index += 2
				secondExpressionParseResult := ParseExpression(index, false)
				if secondExpressionParseResult.GotResult {
					index += secondExpressionParseResult.TokensConsumed
					return inPlaceMathOperationParseResult(index, expressionParseResult, secondExpressionParseResult, AstKind_DivisionExpression)
				} else {
					return ParseResult{
						GotResult:  true,
						Error:      errors.New("Incomplete /="),
						LineNumber: secondExpressionParseResult.LineNumber,
					}
				}
			} else if GetKind(index) == TokenKind_LeftSquareBracket {
				index += 1
				if secondExpressionParseResult := ParseExpression(index, allowInvocations); secondExpressionParseResult.GotResult {
					index += secondExpressionParseResult.TokensConsumed
					if GetKind(index) == TokenKind_RightSquareBracket {
						index += 1
						arrayAccessNode := AstNode{
							Kind: AstKind_ArrayAccess,
							Data: AstData_ArrayAccess{
								// Array is the indexed base (e.g. the `(<x>)` before
								// `[i]`); Index is the subscript expression. (Previously
								// both were set to the subscript, dropping the base and
								// corrupting `(...)[...]` access bytecode.)
								Array: expressionParseResult.Node,
								Index: secondExpressionParseResult.Node,
							},
						}
						consumedSoFar := expressionParseResult.TokensConsumed + 1 + secondExpressionParseResult.TokensConsumed + 1
						// Postfix member access on a subscript: `base[idx].member`
						// (e.g. `CHAPTER_INFO [<i>].level`). The subscript binds first,
						// then the `.member` selects into the indexed element. Without
						// this the `.member` is orphaned and the surrounding `(...)`
						// fails to parse.
						if GetKind(index) == TokenKind_Dot {
							index++
							memberParseResult := ParseExpression(index, true)
							if memberParseResult.GotResult {
								return ParseResult{
									GotResult: true,
									Node: AstNode{
										Kind: AstKind_DotExpression,
										Data: AstData_BinaryExpression{
											LeftNode:  arrayAccessNode,
											RightNode: memberParseResult.Node,
										},
									},
									TokensConsumed: consumedSoFar + 1 + memberParseResult.TokensConsumed,
								}
							}
						}
						return ParseResult{
							GotResult:      true,
							Node:           arrayAccessNode,
							TokensConsumed: consumedSoFar,
						}
					}
				}
			}
		}

		return expressionParseResult
	}

	ParseExpressionBeginningWithLeftParenthesis = func(index int) ParseResult {
		oldIndex := index
		index++

		firstParseResult := ParseExpression(index, true)
		if firstParseResult.GotResult {
			index += firstParseResult.TokensConsumed
			if GetKind(index) == TokenKind_RightParenthesis {
				return ParseResult{
					GotResult: true,
					Node: AstNode{
						Kind: AstKind_UnaryExpression,
						Data: AstData_UnaryExpression{
							Node: firstParseResult.Node,
						},
					},
					TokensConsumed: 2 + firstParseResult.TokensConsumed,
				}
			}

			if GetKind(index) == TokenKind_Comma {
				index++
				if GetKind(index) == TokenKind_Float || GetKind(index) == TokenKind_Minus && GetKind(index+1) == TokenKind_Float {
					secondParseResult := ParseExpression(index, true)
					if secondParseResult.GotResult {
						index += secondParseResult.TokensConsumed
						if GetKind(index) == TokenKind_RightParenthesis {
							return ParseResult{
								GotResult: true,
								Node: AstNode{
									Kind: AstKind_Pair,
									Data: AstData_Pair{
										FloatNodeA: firstParseResult.Node,
										FloatNodeB: secondParseResult.Node,
									},
								},
								TokensConsumed: 3 + firstParseResult.TokensConsumed + secondParseResult.TokensConsumed,
							}
						}
						if GetKind(index) == TokenKind_Comma {
							index++
							if GetKind(index) == TokenKind_Float || GetKind(index) == TokenKind_Minus && GetKind(index+1) == TokenKind_Float {
								thirdParseResult := ParseExpression(index, true)
								if thirdParseResult.GotResult {
									index += thirdParseResult.TokensConsumed
									if GetKind(index) == TokenKind_RightParenthesis {
										return ParseResult{
											GotResult: true,
											Node: AstNode{
												Kind: AstKind_Vector,
												Data: AstData_Vector{
													FloatNodeA: firstParseResult.Node,
													FloatNodeB: secondParseResult.Node,
													FloatNodeC: thirdParseResult.Node,
												},
											},
											TokensConsumed: 4 + firstParseResult.TokensConsumed + secondParseResult.TokensConsumed + thirdParseResult.TokensConsumed,
										}
									}
									return ParseResult{
										GotResult:  true,
										Error:      errors.New("Incomplete vector expression"),
										LineNumber: GetToken(oldIndex).LineNumber,
									}
								}
							}
							return ParseResult{
								GotResult:  true,
								Error:      errors.New("Incomplete vector expression"),
								LineNumber: GetToken(oldIndex).LineNumber,
							}
						}
						return ParseResult{
							GotResult:  true,
							Error:      errors.New("Incomplete pair expression"),
							LineNumber: GetToken(oldIndex).LineNumber,
						}
					}
				}
				return ParseResult{
					GotResult:  true,
					Error:      errors.New("Incomplete pair expression"),
					LineNumber: GetToken(oldIndex).LineNumber,
				}
			}

			detectOperator := func(at int) (AstKind, int, bool) {
				switch {
				case GetKind(at) == TokenKind_Plus:
					return AstKind_AdditionExpression, 1, true
				case GetKind(at) == TokenKind_Minus:
					return AstKind_SubtractionExpression, 1, true
				case GetKind(at) == TokenKind_Asterisk:
					return AstKind_MultiplicationExpression, 1, true
				case GetKind(at) == TokenKind_ForwardSlash:
					return AstKind_DivisionExpression, 1, true
				case GetKind(at) == TokenKind_Bang && GetKind(at+1) == TokenKind_Equals:
					return AstKind_NotEqualExpression, 2, true
				case GetKind(at) == TokenKind_LeftAngleBracket && GetKind(at+1) == TokenKind_Equals:
					return AstKind_LessThanEqualsExpression, 2, true
				case GetKind(at) == TokenKind_RightAngleBracket && GetKind(at+1) == TokenKind_Equals:
					return AstKind_GreaterThanEqualsExpression, 2, true
				case GetKind(at) == TokenKind_RightAngleBracket:
					return AstKind_GreaterThanExpression, 1, true
				case GetKind(at) == TokenKind_LeftAngleBracket:
					return AstKind_LessThanExpression, 1, true
				case GetKind(at) == TokenKind_Equals:
					return AstKind_EqualsExpression, 1, true
				case GetKind(at) == TokenKind_And:
					return AstKind_LogicalAnd, 1, true
				case GetKind(at) == TokenKind_Or:
					return AstKind_LogicalOr, 1, true
				}
				return 0, 0, false
			}

			// flattenLogicalSpine appends an operand subtree to the flat operand/
			// operator lists, splitting any or/and that ParseExpression already grabbed
			// (it treats or/and as right-associative postfix) so a whole parenthesised
			// chain like (A = 0 or B = <c>) becomes one flat infix sequence.
			var flattenLogicalSpine func(n AstNode, operands *[]AstNode, operators *[]AstKind, nlBefore, nlAfter *[]int)
			flattenLogicalSpine = func(n AstNode, operands *[]AstNode, operators *[]AstKind, nlBefore, nlAfter *[]int) {
				if n.Kind == AstKind_LogicalOr || n.Kind == AstKind_LogicalAnd {
					data := n.Data.(AstData_BinaryExpression)
					flattenLogicalSpine(data.LeftNode, operands, operators, nlBefore, nlAfter)
					*operators = append(*operators, n.Kind)
					*nlBefore = append(*nlBefore, 0) // spine operators carry no source newline
					*nlAfter = append(*nlAfter, 0)
					flattenLogicalSpine(data.RightNode, operands, operators, nlBefore, nlAfter)
					return
				}
				*operands = append(*operands, n)
			}

			handleBinaryOperator := func(astKind AstKind, size int) ParseResult {
				// firstParseResult (left operand) and the first operator (astKind/size)
				// have already been recognised by the caller. Parse the rest of the flat
				// infix sequence up to ')'. A single operator yields the original binary
				// node (byte-identical to before); two or more yield a flat node, matching
				// THUG2's single-0xE/0xF-pair encoding for chained conditions.
				var operands []AstNode
				var operators []AstKind
				var newlinesBeforeOperator []int
				var newlinesAfterOperator []int
				flattenLogicalSpine(firstParseResult.Node, &operands, &operators, &newlinesBeforeOperator, &newlinesAfterOperator)
				consumed := 1 + firstParseResult.TokensConsumed

				parseOperand := func() bool {
					operandResult := ParseExpression(index, true)
					if !operandResult.GotResult {
						return false
					}
					index += operandResult.TokensConsumed
					consumed += operandResult.TokensConsumed
					flattenLogicalSpine(operandResult.Node, &operands, &operators, &newlinesBeforeOperator, &newlinesAfterOperator)
					return true
				}

				// skipNewlines consumes + counts newlines that sit between an operator
				// byte and the following operand (`(A +\n B)`, 0x01 after the operator).
				skipNewlines := func() int {
					n := 0
					for GetKind(index) == TokenKind_NewLine {
						n++
						index++
						consumed++
					}
					return n
				}

				operators = append(operators, astKind)
				newlinesBeforeOperator = append(newlinesBeforeOperator, 0) // first operator already recognised by caller
				index += size
				consumed += size
				newlinesAfterOperator = append(newlinesAfterOperator, skipNewlines())
				if !parseOperand() {
					return ParseResult{GotResult: false, Reason: "Couldn't parse binary operator expression"}
				}

				trailingNewlines := 0
				for GetKind(index) != TokenKind_RightParenthesis {
					// Newlines inside a parenthesised flat expression sit before an
					// operator (`(A * B\n + C)`, stored as 0x01 before the operator
					// byte). Consume + count them; they must NOT abort the operator
					// scan as an unrecognised token.
					nlCount := 0
					for GetKind(index) == TokenKind_NewLine {
						nlCount++
						index++
						consumed++
					}
					if GetKind(index) == TokenKind_RightParenthesis {
						trailingNewlines = nlCount // newlines between last operand and ')'
						break
					}
					operatorKind, operatorSize, ok := detectOperator(index)
					if !ok {
						return ParseResult{GotResult: false, Reason: "Couldn't parse binary operator expression"}
					}
					operators = append(operators, operatorKind)
					newlinesBeforeOperator = append(newlinesBeforeOperator, nlCount)
					index += operatorSize
					consumed += operatorSize
					newlinesAfterOperator = append(newlinesAfterOperator, skipNewlines())
					if !parseOperand() {
						return ParseResult{GotResult: false, Reason: "Couldn't parse binary operator expression"}
					}
				}
				consumed += 1

				hasNewlines := trailingNewlines > 0
				for _, n := range newlinesBeforeOperator {
					if n > 0 {
						hasNewlines = true
					}
				}
				for _, n := range newlinesAfterOperator {
					if n > 0 {
						hasNewlines = true
					}
				}

				// A single operator yields the original binary node (byte-identical),
				// UNLESS a newline precedes it — that must round-trip via the flat node,
				// which records newline positions.
				if len(operators) == 1 && !hasNewlines {
					return ParseResult{
						GotResult: true,
						Node: AstNode{
							Kind: astKind,
							Data: AstData_BinaryExpression{
								LeftNode:  operands[0],
								RightNode: operands[1],
							},
						},
						TokensConsumed: consumed,
					}
				}

				for _, operatorKind := range operators {
					if _, ok := FlatOperatorByte(operatorKind); !ok {
						return ParseResult{GotResult: false, Reason: "Couldn't parse binary operator expression"}
					}
				}
				return ParseResult{
					GotResult: true,
					Node: AstNode{
						Kind: AstKind_FlatExpression,
						Data: AstData_FlatExpression{
							Operands:               operands,
							Operators:              operators,
							NewlinesBeforeOperator: newlinesBeforeOperator,
							NewlinesAfterOperator:  newlinesAfterOperator,
							TrailingNewlines:       trailingNewlines,
						},
					},
					TokensConsumed: consumed,
				}
			}
			// Adjacent operands with NO operator between them, e.g. `(<x> -1)`
			// where `-1` lexes as a single signed-literal token. THUG2 encodes
			// this as 0xE <x> <int -1> 0xF (no operator byte), distinct from the
			// binary subtraction `(<x> - 1)`. Build a FlatExpression whose operator
			// slice is shorter than operands-1 so output emits no joining operator
			// byte for the adjacency.
			isOperandStart := func(at int) bool {
				switch GetKind(at) {
				case TokenKind_Integer, TokenKind_Float, TokenKind_String,
					TokenKind_LocalString, TokenKind_LeftAngleBracket,
					TokenKind_Identifier, TokenKind_RawChecksum:
					return true
				}
				return false
			}
			if isOperandStart(index) {
				operands := []AstNode{firstParseResult.Node}
				operators := []AstKind{}
				consumed := 1 + firstParseResult.TokensConsumed
				for isOperandStart(index) {
					operandResult := ParseExpression(index, true)
					if !operandResult.GotResult {
						break
					}
					index += operandResult.TokensConsumed
					consumed += operandResult.TokensConsumed
					operands = append(operands, operandResult.Node)
				}
				if GetKind(index) == TokenKind_RightParenthesis {
					consumed += 1
					return ParseResult{
						GotResult: true,
						Node: AstNode{
							Kind: AstKind_FlatExpression,
							Data: AstData_FlatExpression{
								Operands:  operands,
								Operators: operators,
							},
						},
						TokensConsumed: consumed,
					}
				}
			}
			if GetKind(index) == TokenKind_Plus {
				return handleBinaryOperator(AstKind_AdditionExpression, 1)
			}
			if GetKind(index) == TokenKind_Minus {
				return handleBinaryOperator(AstKind_SubtractionExpression, 1)
			}
			if GetKind(index) == TokenKind_Asterisk {
				return handleBinaryOperator(AstKind_MultiplicationExpression, 1)
			}
			if GetKind(index) == TokenKind_ForwardSlash {
				return handleBinaryOperator(AstKind_DivisionExpression, 1)
			}
			if GetKind(index) == TokenKind_Bang && GetKind(index+1) == TokenKind_Equals {
				return handleBinaryOperator(AstKind_NotEqualExpression, 2)
			}
			if GetKind(index) == TokenKind_LeftAngleBracket && GetKind(index+1) == TokenKind_Equals {
				return handleBinaryOperator(AstKind_LessThanEqualsExpression, 2)
			}
			if GetKind(index) == TokenKind_RightAngleBracket && GetKind(index+1) == TokenKind_Equals {
				return handleBinaryOperator(AstKind_GreaterThanEqualsExpression, 2)
			}
			if GetKind(index) == TokenKind_RightAngleBracket {
				return handleBinaryOperator(AstKind_GreaterThanExpression, 1)
			}
			if GetKind(index) == TokenKind_LeftAngleBracket {
				return handleBinaryOperator(AstKind_LessThanExpression, 1)
			}
			if GetKind(index) == TokenKind_Equals {
				return handleBinaryOperator(AstKind_EqualsExpression, 1)
			}
			if GetKind(index) == TokenKind_And {
				return handleBinaryOperator(AstKind_LogicalAnd, 1)
			}
			if GetKind(index) == TokenKind_Or {
				return handleBinaryOperator(AstKind_LogicalOr, 1)
			}
		}

		return ParseResult{
			GotResult:  true,
			Error:      errors.New("Incomplete parenthesis ("),
			LineNumber: GetToken(oldIndex).LineNumber,
			Reason:     TokensNotRecognisedError(parser.Tokens[oldIndex:], "an expression beginning with a left parenthesis"),
		}
	}

	ParseChecksumOrInvocation = func(index int, allowInvocations bool) ParseResult {
		var checksumOrInvocation ParseResult

		if allowInvocations {
			extraTokens := 0
			for {
				if GetKind(index+extraTokens+1) == TokenKind_BackwardSlash && GetKind(index+extraTokens+2) == TokenKind_NewLine {
					extraTokens += 2
				} else {
					break
				}
			}
			// A binary minus after the checksum (e.g. `(Foo - 1)`) is a
			// subtraction operator, never the start of an invocation argument.
			// It only reaches here as TokenKind_Minus in the spaced form; the
			// adjacency form `Foo -1` lexes as a single negative Integer, so real
			// signed-literal arguments are unaffected. Without this guard,
			// parseExpressionInner folds `- 1` into the literal `-1` and wrongly
			// makes Foo an invocation, dropping the 0x0A subtraction byte.
			if GetKind(index+extraTokens+1) != TokenKind_Minus {
				if expressionParseResult := ParseExpression(index+extraTokens+1, allowInvocations); expressionParseResult.GotResult {
					checksumOrInvocation = ParseInvocation(index)
				}
			}
		}

		if checksumOrInvocation.GotResult == false {
			checksumOrInvocation = ParseChecksum(index)
		} else if checksumOrInvocation.Error != nil {
			return checksumOrInvocation
		}

		if checksumOrInvocation.GotResult == false {
			return ParseResult{
				GotResult: false,
				Reason:    TokensNotRecognisedError(parser.Tokens[index:], "an invocation or checksum node"),
			}
		}

		return checksumOrInvocation
	}

	ParseChecksum = func(index int) ParseResult {
		if GetKind(index) == TokenKind_LeftAngleBracket &&
			GetKind(index+2) == TokenKind_RightAngleBracket {
			return ParseResult{
				GotResult: true,
				Node: AstNode{
					Kind: AstKind_LocalReference,
					Data: AstData_LocalReference{
						Node: AstNode{
							Kind: AstKind_Checksum,
							Data: AstData_Checksum{
								ChecksumToken: GetToken(index + 1),
								IsRawChecksum: GetKind(index+1) == TokenKind_RawChecksum,
							},
						},
					},
				},
				TokensConsumed: 3,
			}
		}
		return ParseResult{
			GotResult: true,
			Node: AstNode{
				Kind: AstKind_Checksum,
				Data: AstData_Checksum{
					ChecksumToken: GetToken(index),
					IsRawChecksum: GetKind(index) == TokenKind_RawChecksum,
				},
			},
			TokensConsumed: 1,
		}
	}

	ParseFloat = func(index int) ParseResult {
		return ParseResult{
			GotResult: true,
			Node: AstNode{
				Kind: AstKind_Float,
				Data: AstData_Float{
					FloatToken: GetToken(index),
				},
			},
			TokensConsumed: 1,
		}
	}

	ParseInteger = func(index int) ParseResult {
		return ParseResult{
			GotResult: true,
			Node: AstNode{
				Kind: AstKind_Integer,
				Data: AstData_Integer{
					IntegerToken: GetToken(index),
				},
			},
			TokensConsumed: 1,
		}
	}

	ParseString = func(index int) ParseResult {
		return ParseResult{
			GotResult: true,
			Node: AstNode{
				Kind: AstKind_String,
				Data: AstData_String{
					StringToken: GetToken(index),
				},
			},
			TokensConsumed: 1,
		}
	}

	ParseArray = func(index int) ParseResult {
		startIndex := index
		index++

		// gather array elements
		var elementNodes AstNodeBuffer
		for {
			if GetKind(index) == TokenKind_BackwardSlash && GetKind(index+1) == TokenKind_NewLine {
				index += 2
				elementNodes.TokensConsumed += 2
			} else if GetKind(index) == TokenKind_RightSquareBracket {
				break
			} else if GetKind(index) == TokenKind_SingleLineComment || GetKind(index) == TokenKind_MultiLineComment {
				index++
				elementNodes.TokensConsumed++
			} else if newLineParseResult := ParseNewLine(index); newLineParseResult.GotResult {
				elementNodes.MaybeSave(newLineParseResult)
				index += newLineParseResult.TokensConsumed
			} else if commaParseResult := ParseComma(index); commaParseResult.GotResult {
				elementNodes.MaybeSave(commaParseResult)
				index += commaParseResult.TokensConsumed
			} else if expressionParseResult := ParseExpression(index, true); expressionParseResult.GotResult && expressionParseResult.TokensConsumed > 0 {
				// Require forward progress: a GotResult with 0 tokens consumed (e.g. a
				// nested-struct element that hit an "Incomplete struct" and returned
				// without a token count) would otherwise spin this loop forever.
				elementNodes.MaybeSave(expressionParseResult)
				index += expressionParseResult.TokensConsumed
			} else {
				return ParseResult{
					GotResult:  true,
					Error:      errors.New("Incomplete array"),
					LineNumber: GetToken(startIndex).LineNumber,
				}
			}
		}

		return ParseResult{
			GotResult: true,
			Node: AstNode{
				Kind: AstKind_Array,
				Data: AstData_Array{
					ElementNodes: elementNodes.Nodes,
				},
			},
			TokensConsumed: 2 + elementNodes.TokensConsumed,
		}
	}

	ParseStruct = func(index int) ParseResult {
		var elementNodes AstNodeBuffer

		if GetKind(index) != TokenKind_LeftCurlyBrace {
			return ParseResult{
				GotResult: false,
				Reason:    "Couldn't parse struct, no '{' found",
			}
		}

		startIndex := index

		index++
		indexAfterLastIteration := index
		for { // gather struct elements
			if GetKind(index) == TokenKind_BackwardSlash && GetKind(index+1) == TokenKind_NewLine {
				elementNodes.TokensConsumed += 2
				index += 2
			} else if parseResult := ParseNewLine(index); parseResult.GotResult {
				elementNodes.MaybeSave(parseResult)
				index += parseResult.TokensConsumed
			} else if GetKind(index) == TokenKind_If {
				return ParseResult{
					GotResult: false,
					Reason:    "Failed to parse struct elements, found 'if'",
				}
			} else if GetKind(index) == TokenKind_While {
				return ParseResult{
					GotResult: false,
					Reason:    "Failed to parse struct elements, found 'while'",
				}
			} else if GetKind(index) == TokenKind_Switch {
				// Control-flow, never struct content. Bail so a `{ switch ... }`
				// after a function-call if-condition is recognised as the if-BODY,
				// not greedily (mis)absorbed as a struct argument.
				return ParseResult{
					GotResult: false,
					Reason:    "Failed to parse struct elements, found 'switch'",
				}
			} else if GetKind(index) == TokenKind_Break {
				// Control-flow, never struct content. Like 'if'/'while' above, bail so
				// a `{ break ... }` after a function-call if-condition is recognised as
				// the if-BODY, not greedily (mis)absorbed as a struct argument.
				return ParseResult{
					GotResult: false,
					Reason:    "Failed to parse struct elements, found 'break'",
				}
			} else if GetKind(index) == TokenKind_Return {
				return ParseResult{
					GotResult: false,
					Reason:    "Failed to parse struct elements, found 'return'",
				}
			} else if GetKind(index) == TokenKind_Begin {
				// Control-flow (counted `Begin { ... } Repeat <n>` loop), never struct
				// content. Bail so a `{ Begin ... }` after a function-call if-condition is
				// recognised as the if-BODY, not greedily (mis)absorbed as a struct argument.
				return ParseResult{
					GotResult: false,
					Reason:    "Failed to parse struct elements, found 'Begin'",
				}
			} else if GetKind(index) == TokenKind_Repeat {
				return ParseResult{
					GotResult: false,
					Reason:    "Failed to parse struct elements, found 'Repeat'",
				}
			} else if GetKind(index) == TokenKind_RightCurlyBrace {
				break
			} else if parseResult := ParseComma(index); parseResult.GotResult {
				elementNodes.MaybeSave(parseResult)
				index += parseResult.TokensConsumed
			} else if parseResult := ParseComment(index); parseResult.GotResult {
				elementNodes.MaybeSave(parseResult)
				index += parseResult.TokensConsumed
			} else if GetKind(index) == TokenKind_Script {
				// A struct member can be a nested script definition, e.g. the
				// `script disqualify_script { ... }` inside each cas_skater part
				// struct. ParseAssignment/ParseExpression don't handle the `script`
				// keyword, so without this the loop made no progress — and inside an
				// array element that 0-token result spun ParseArray forever.
				parseResult := ParseScript(index)
				if !parseResult.GotResult || parseResult.Error != nil {
					return parseResult
				}
				elementNodes.MaybeSave(parseResult)
				index += parseResult.TokensConsumed
			} else if parseResult := ParseAssignment(index, true); parseResult.GotResult {
				if parseResult.Error != nil {
					return parseResult
				}
				elementNodes.MaybeSave(parseResult)
				index += parseResult.TokensConsumed
			} else if parseResult := ParseExpression(index, true); parseResult.GotResult {
				elementNodes.MaybeSave(parseResult)
				index += parseResult.TokensConsumed
			} else if index == indexAfterLastIteration {
				return ParseResult{
					GotResult:  true,
					Error:      errors.New("Incomplete struct"),
					LineNumber: GetToken(startIndex).LineNumber,
					Reason:     TokensNotRecognisedError(parser.Tokens[index:], "a struct element"),
				}
			}
			indexAfterLastIteration = index
		}

		return ParseResult{
			GotResult: true,
			Node: AstNode{
				Kind: AstKind_Struct,
				Data: AstData_Struct{
					ElementNodes: elementNodes.Nodes,
				},
			},
			TokensConsumed: 2 + elementNodes.TokensConsumed,
		}
	}

	ParseAssignment = func(index int, allowInvocations bool) ParseResult {
		start := index
		// ParseChecksum will blindly accept ANY token as a checksum name
		// (including punctuation like ')'); guard against that so an invocation
		// parameter loop doesn't mistake a closing ')' followed by '= <x>' for a
		// keyword-param assignment `)=<x>` and swallow tokens past the paren. A
		// real assignment target starts with a name: identifier, raw checksum,
		// or a `<local>` reference.
		if GetKind(index) != TokenKind_Identifier &&
			GetKind(index) != TokenKind_RawChecksum &&
			GetKind(index) != TokenKind_LeftAngleBracket {
			return ParseResult{
				GotResult: false,
				Reason:    "First token in assignment wasn't a name",
			}
		}
		nameParseResult := ParseChecksum(index)
		if !nameParseResult.GotResult {
			return ParseResult{
				GotResult: false,
				Reason:    "First token in assignment wasn't a checksum",
			}
		}
		index += nameParseResult.TokensConsumed

		// Newlines may sit between the name and '=' (`name\n= value`, stored as 0x01
		// before the 0x07). Count them so output can re-emit them; if no '=' follows,
		// this isn't an assignment and we fall through to the not-equals return.
		newlinesBeforeEquals := 0
		for GetKind(index) == TokenKind_NewLine {
			newlinesBeforeEquals++
			index++
		}

		if GetKind(index) != TokenKind_Equals {
			return ParseResult{
				GotResult: false,
				Reason:    "Second token in assignment wasn't an 'equals'",
			}
		}
		index++

		// The value may sit on a later line (e.g. a multi-line array/struct
		// assignment: `name =\n{ ... }`). THUG2 encodes each such newline as a 0x01
		// after the 0x07, so count them here and re-emit them in output (byte-
		// identity). Comments carry no bytecode, so they are skipped without count.
		newlinesAfterEquals := 0
		for GetKind(index) == TokenKind_NewLine ||
			GetKind(index) == TokenKind_SingleLineComment ||
			GetKind(index) == TokenKind_MultiLineComment {
			if GetKind(index) == TokenKind_NewLine {
				newlinesAfterEquals++
			}
			index++
		}

		valueParseResult := ParseExpression(index, allowInvocations)
		if !valueParseResult.GotResult {
			return ParseResult{
				GotResult:  true,
				Error:      errors.New("Incomplete assignment"),
				LineNumber: GetToken(index - 1).LineNumber,
				Reason:     WrapStr("Couldn't parse expression for value of assignment", valueParseResult.Reason),
			}
		} else if valueParseResult.Error != nil {
			return valueParseResult
		}
		index += valueParseResult.TokensConsumed

		return ParseResult{
			GotResult: true,
			Node: AstNode{
				Kind: AstKind_Assignment,
				Data: AstData_Assignment{
					NameNode:             nameParseResult.Node,
					ValueNode:            valueParseResult.Node,
					NewlinesAfterEquals:  newlinesAfterEquals,
					NewlinesBeforeEquals: newlinesBeforeEquals,
				},
			},
			TokensConsumed: index - start,
		}
	}

	pruneStructIfInvoked := func(parseResult *ParseResult, index *int) {
		// If the if-body `{...}` is still present at the cursor, the invocation did
		// NOT swallow it as a struct param (e.g. the body contains a `return`/`break`,
		// which makes ParseStruct bail), so the invocation's last struct param is a
		// real ARGUMENT — `if Foo {arg=<x>} { ... return }` — not the body. Pruning it
		// here would steal the real argument and mis-read it as the body. Leave it.
		if GetKind(*index) == TokenKind_LeftCurlyBrace {
			return
		}
		// TODO(brandon): semantically compress this code, it does basically the same thing twice but for 2 cases
		if parseResult.Node.Kind == AstKind_Invocation {
			invocationData := parseResult.Node.Data.(AstData_Invocation)
			lastParameterIndex := len(invocationData.ParameterNodes) - 1
			lastParameterNode := invocationData.ParameterNodes[lastParameterIndex]
			if lastParameterNode.Kind == AstKind_Struct {
				// remove struct from params
				parseResult.Node.Data = AstData_Invocation{
					ScriptIdentifierNode:              invocationData.ScriptIdentifierNode,
					ParameterNodes:                    invocationData.ParameterNodes[:lastParameterIndex],
					TokensConsumedByEachParameterNode: invocationData.TokensConsumedByEachParameterNode[:lastParameterIndex],
				}

				// skip backwards over the struct so it will be read as the if-statement body
				*index -= invocationData.TokensConsumedByEachParameterNode[lastParameterIndex]

				// reduce the number of tokens consumed by the condition
				parseResult.TokensConsumed -= invocationData.TokensConsumedByEachParameterNode[lastParameterIndex]
			}
		} else if parseResult.Node.Kind == AstKind_LogicalNot {
			logicalNotData := parseResult.Node.Data.(AstData_UnaryExpression)
			if logicalNotData.Node.Kind == AstKind_Invocation {
				invocationData := logicalNotData.Node.Data.(AstData_Invocation)
				lastParameterIndex := len(invocationData.ParameterNodes) - 1
				lastParameterNode := invocationData.ParameterNodes[lastParameterIndex]
				if lastParameterNode.Kind == AstKind_Struct {
					// remove struct from params
					parseResult.Node.Data = AstData_UnaryExpression{
						Node: AstNode{
							Kind: AstKind_Invocation,
							Data: AstData_Invocation{
								ScriptIdentifierNode:              invocationData.ScriptIdentifierNode,
								ParameterNodes:                    invocationData.ParameterNodes[:lastParameterIndex],
								TokensConsumedByEachParameterNode: invocationData.TokensConsumedByEachParameterNode[:lastParameterIndex],
							},
						},
					}
					// skip backwards over the struct so it will be read as the if-statement body
					*index -= invocationData.TokensConsumedByEachParameterNode[lastParameterIndex]
					// reduce the number of tokens consumed by the condition
					parseResult.TokensConsumed -= invocationData.TokensConsumedByEachParameterNode[lastParameterIndex]
				}
			} else if logicalNotData.Node.Kind == AstKind_ColonExpression {
				colonData := logicalNotData.Node.Data.(AstData_BinaryExpression)
				if colonData.RightNode.Kind == AstKind_Invocation {
					invocationData := colonData.RightNode.Data.(AstData_Invocation)
					lastParameterIndex := len(invocationData.ParameterNodes) - 1
					lastParameterNode := invocationData.ParameterNodes[lastParameterIndex]
					if lastParameterNode.Kind == AstKind_Struct {
						// `if ! <obj>:<method> { body }` — the colon RHS invocation
						// over-consumed the if-body as a struct arg; prune it back off.
						parseResult.Node.Data = AstData_UnaryExpression{
							Node: AstNode{
								Kind: AstKind_ColonExpression,
								Data: AstData_BinaryExpression{
									LeftNode: colonData.LeftNode,
									RightNode: AstNode{
										Kind: AstKind_Invocation,
										Data: AstData_Invocation{
											ScriptIdentifierNode:              invocationData.ScriptIdentifierNode,
											ParameterNodes:                    invocationData.ParameterNodes[:lastParameterIndex],
											TokensConsumedByEachParameterNode: invocationData.TokensConsumedByEachParameterNode[:lastParameterIndex],
										},
									},
								},
							},
						}
						*index -= invocationData.TokensConsumedByEachParameterNode[lastParameterIndex]
						parseResult.TokensConsumed -= invocationData.TokensConsumedByEachParameterNode[lastParameterIndex]
					}
				}
			}
		} else if parseResult.Node.Kind == AstKind_Assignment {
			assignmentData := parseResult.Node.Data.(AstData_Assignment)
			if assignmentData.ValueNode.Kind == AstKind_Invocation {
				invocationData := assignmentData.ValueNode.Data.(AstData_Invocation)
				lastParameterIndex := len(invocationData.ParameterNodes) - 1
				lastParameterNode := invocationData.ParameterNodes[lastParameterIndex]
				if lastParameterNode.Kind == AstKind_Struct {
					// remove struct from params
					parseResult.Node.Data = AstData_Assignment{
						NameNode: assignmentData.NameNode,
						ValueNode: AstNode{
							Kind: AstKind_Invocation,
							Data: AstData_Invocation{
								ScriptIdentifierNode:              invocationData.ScriptIdentifierNode,
								ParameterNodes:                    invocationData.ParameterNodes[:lastParameterIndex],
								TokensConsumedByEachParameterNode: invocationData.TokensConsumedByEachParameterNode[:lastParameterIndex],
							},
						},
					}
					// skip backwards over the struct so it will be read as the if-statement body
					*index -= invocationData.TokensConsumedByEachParameterNode[lastParameterIndex]
					// reduce the number of tokens consumed by the condition
					parseResult.TokensConsumed -= invocationData.TokensConsumedByEachParameterNode[lastParameterIndex]
				}
			}
		} else if parseResult.Node.Kind == AstKind_ColonExpression {
			colonData := parseResult.Node.Data.(AstData_BinaryExpression)
			if colonData.RightNode.Kind == AstKind_Invocation {
				invocationData := colonData.RightNode.Data.(AstData_Invocation)
				lastParameterIndex := len(invocationData.ParameterNodes) - 1
				lastParameterNode := invocationData.ParameterNodes[lastParameterIndex]
				if lastParameterNode.Kind == AstKind_Struct {
					// remove struct from params
					parseResult.Node.Data = AstData_BinaryExpression{
						LeftNode: colonData.LeftNode,
						RightNode: AstNode{
							Kind: AstKind_Invocation,
							Data: AstData_Invocation{
								ScriptIdentifierNode:              invocationData.ScriptIdentifierNode,
								ParameterNodes:                    invocationData.ParameterNodes[:lastParameterIndex],
								TokensConsumedByEachParameterNode: invocationData.TokensConsumedByEachParameterNode[:lastParameterIndex],
							},
						},
					}

					// skip backwards over the struct so it will be read as the if-statement body
					*index -= invocationData.TokensConsumedByEachParameterNode[lastParameterIndex]
					// reduce the number of tokens consumed by the condition
					parseResult.TokensConsumed -= invocationData.TokensConsumedByEachParameterNode[lastParameterIndex]
				}
			}
		}
	}

	ParseScript = func(index int) ParseResult {
		if GetKind(index) != TokenKind_Script {
			return ParseResult{
				GotResult: false,
				Reason:    "First token in script wasn't 'script'",
			}
		}
		index++

		var defaultParameters AstNodeBuffer

		if GetKind(index) != TokenKind_Identifier && GetKind(index) != TokenKind_RawChecksum {
			return ParseResult{
				GotResult:  true,
				Error:      errors.New("Incomplete script definition"),
				LineNumber: GetToken(index - 1).LineNumber,
				Reason:     "Second token in script wasn't an identifier or a checksum",
			}
		}
		nameToken := GetToken(index)
		index++

		for {
			if GetKind(index) == TokenKind_NewLine {
				index++
			} else if parseResult := ParseAssignment(index, true); parseResult.GotResult {
				index += parseResult.TokensConsumed
				pruneStructIfInvoked(&parseResult, &index)
				defaultParameters.MaybeSave(parseResult)
			} else {
				break
			}
		}

		bodyParseResult, bodyNodes := ParseBodyOfCode(index)
		if !bodyParseResult.GotResult {
			return ParseResult{
				GotResult:  true,
				Error:      errors.New("Incomplete script definition"),
				LineNumber: GetToken(index - 1).LineNumber,
				Reason:     WrapStr("Couldn't parse script body", bodyParseResult.Reason),
			}
		} else if bodyParseResult.Error != nil {
			return bodyParseResult
		}

		return ParseResult{
			GotResult: true,
			Node: AstNode{
				Kind: AstKind_Script,
				Data: AstData_Script{
					NameNode: AstNode{
						Kind: AstKind_Checksum,
						Data: AstData_Checksum{
							ChecksumToken: nameToken,
							IsRawChecksum: nameToken.Kind == TokenKind_RawChecksum,
						},
					},
					DefaultParameterNodes: defaultParameters.Nodes,
					BodyNodes:             bodyNodes,
				},
			},
			TokensConsumed: 1 + defaultParameters.TokensConsumed + bodyParseResult.TokensConsumed + 1,
		}
	}

	ParseWhileLoop = func(index int) ParseResult {
		if GetKind(index) != TokenKind_While {
			return ParseResult{
				GotResult: false,
				Reason:    "First token in while loop wasn't 'while'",
			}
		}
		index++

		bodyParseResult, bodyNodes := ParseBodyOfCode(index)
		if bodyParseResult.GotResult {
			return ParseResult{
				GotResult: true,
				Node: AstNode{
					Kind: AstKind_WhileLoop,
					Data: AstData_WhileLoop{
						BodyNodes: bodyNodes,
					},
				},
				TokensConsumed: 1 + bodyParseResult.TokensConsumed,
			}
		}

		return ParseResult{
			GotResult: false,
			Reason:    WrapStr("Couldn't parse while loop body", bodyParseResult.Reason),
		}
	}

	ParseRepeatLoop = func(index int) ParseResult {
		// Counted loop: `Begin { <body> } Repeat <count>`.
		if GetKind(index) != TokenKind_Begin {
			return ParseResult{
				GotResult: false,
				Reason:    "First token in repeat loop wasn't 'Begin'",
			}
		}
		startIndex := index
		index++

		bodyParseResult, bodyNodes := ParseBodyOfCode(index)
		if !bodyParseResult.GotResult {
			return ParseResult{
				GotResult: false,
				Reason:    WrapStr("Couldn't parse Begin loop body", bodyParseResult.Reason),
			}
		}
		index += bodyParseResult.TokensConsumed

		if GetKind(index) != TokenKind_Repeat {
			return ParseResult{
				GotResult: false,
				Reason:    "Expected 'Repeat' after Begin loop body",
			}
		}
		index++

		// Optional repeat-count expression (e.g. `Repeat <num_blanks>` or `Repeat 16`).
		var countNode AstNode
		hasCount := false
		if countResult := ParseExpression(index, false); countResult.GotResult && countResult.Error == nil {
			countNode = countResult.Node
			hasCount = true
			index += countResult.TokensConsumed
		}

		return ParseResult{
			GotResult: true,
			Node: AstNode{
				Kind: AstKind_RepeatLoop,
				Data: AstData_RepeatLoop{
					BodyNodes: bodyNodes,
					CountNode: countNode,
					HasCount:  hasCount,
				},
			},
			TokensConsumed: index - startIndex,
		}
	}

	ParseSwitch = func(index int) ParseResult {
		// Native THUG2 switch:
		//   switch <value>
		//     case <value> { <body> }
		//     ...
		//     default { <body> }
		//   endswitch
		oldIndex := index
		if GetKind(index) != TokenKind_Switch {
			return ParseResult{
				GotResult: false,
				Reason:    "First token in switch wasn't 'switch'",
			}
		}
		index++

		valueParseResult := ParseExpression(index, false)
		if !valueParseResult.GotResult {
			return ParseResult{
				GotResult: false,
				Reason:    WrapStr("Couldn't parse switch value", valueParseResult.Reason),
			}
		}
		index += valueParseResult.TokensConsumed

		// Skip formatting between the value and the first case, returning the count of
		// newlines (which ARE bytecode — 0x01 — and must be preserved; comments are
		// not). Most switches have a single newline after the value; some have extra.
		skipFormatting := func() int {
			n := 0
			for {
				switch GetKind(index) {
				case TokenKind_NewLine:
					n++
					index++
				case TokenKind_SingleLineComment, TokenKind_MultiLineComment:
					index++
				default:
					return n
				}
			}
		}

		newlinesAfterValue := skipFormatting()

		var caseValues []AstNode
		var caseBodies [][]AstNode
		hasDefault := false
		var defaultBody []AstNode

		for {
			skipFormatting()

			if GetKind(index) == TokenKind_EndSwitch {
				index++
				break
			} else if GetKind(index) == TokenKind_Case {
				index++
				caseValueParseResult := ParseExpression(index, false)
				if !caseValueParseResult.GotResult {
					return ParseResult{
						GotResult: false,
						Reason:    WrapStr("Couldn't parse case value", caseValueParseResult.Reason),
					}
				}
				index += caseValueParseResult.TokensConsumed

				bodyParseResult, bodyNodes := ParseBodyOfCode(index)
				if !bodyParseResult.GotResult {
					return ParseResult{
						GotResult: false,
						Reason:    WrapStr("Couldn't parse case body", bodyParseResult.Reason),
					}
				}
				index += bodyParseResult.TokensConsumed

				caseValues = append(caseValues, caseValueParseResult.Node)
				caseBodies = append(caseBodies, bodyNodes)
			} else if GetKind(index) == TokenKind_Default {
				index++
				bodyParseResult, bodyNodes := ParseBodyOfCode(index)
				if !bodyParseResult.GotResult {
					return ParseResult{
						GotResult: false,
						Reason:    WrapStr("Couldn't parse default body", bodyParseResult.Reason),
					}
				}
				index += bodyParseResult.TokensConsumed
				hasDefault = true
				defaultBody = bodyNodes
			} else {
				return ParseResult{
					GotResult: false,
					Reason:    fmt.Sprintf("Unexpected token in switch body, '%s'. Expected 'case', 'default', or 'endswitch'.", GetToken(index).Data),
				}
			}
		}

		return ParseResult{
			GotResult: true,
			Node: AstNode{
				Kind: AstKind_Switch,
				Data: AstData_Switch{
					ValueNode:          valueParseResult.Node,
					CaseValues:         caseValues,
					CaseBodies:         caseBodies,
					HasDefault:         hasDefault,
					DefaultBody:        defaultBody,
					NewlinesAfterValue: newlinesAfterValue,
				},
			},
			TokensConsumed: index - oldIndex,
		}
	}

	ParseLogicalNot = func(index int) ParseResult {
		if GetKind(index) != TokenKind_Bang {
			return ParseResult{
				GotResult: false,
				Reason:    "Logical not doesn't start with '!'",
			}
		}
		index++
		expressionParseResult := ParseExpression(index, true)
		if !expressionParseResult.GotResult {
			return ParseResult{
				GotResult: false,
				Reason:    "Failed to parse expression after logical-not",
			}
		}
		return ParseResult{
			GotResult: true,
			Node: AstNode{
				Kind: AstKind_LogicalNot,
				Data: AstData_UnaryExpression{
					Node: expressionParseResult.Node,
				},
			},
			TokensConsumed: 1 + expressionParseResult.TokensConsumed,
		}
	}

	ParseIfStatement = func(index int) ParseResult {
		oldIndex := index
		if GetKind(index) != TokenKind_If {
			return ParseResult{
				GotResult: false,
				Reason:    "First token in if-statement wasn't 'if'",
			}
		}

		index++

		conditions := make([]AstNode, 6500)
		numConditions := 0
		saveCondition := func(conditionParseResult ParseResult) {
			conditions[numConditions] = conditionParseResult.Node
			numConditions++
		}

		bodies := make([][]AstNode, 6500)
		numBodies := 0
		saveBody := func(bodyParseResult ParseResult, bodyNodes []AstNode) {
			bodies[numBodies] = bodyNodes
			numBodies++
		}

		// A parenless comparison condition `if name = value {` is stored as a bare
		// assignment node (0x16<name> 0x07 <value>, no surrounding parens), which
		// ParseExpression won't produce (it stops after the name). Try ParseAssignment
		// first; it only matches `name = ...` and otherwise leaves invocations,
		// parenthesised conditions, `! cond`, and bare checksums to ParseExpression.
		conditionParseResult := ParseAssignment(index, true)
		if !conditionParseResult.GotResult || conditionParseResult.Error != nil {
			conditionParseResult = ParseExpression(index, true)
		}
		if !conditionParseResult.GotResult {
			return ParseResult{
				GotResult: false,
				Reason:    WrapStr("Couldn't parse condition in if-statement", conditionParseResult.Reason),
			}
		}
		index += conditionParseResult.TokensConsumed
		pruneStructIfInvoked(&conditionParseResult, &index)
		saveCondition(conditionParseResult)

		bodyParseResult, bodyNodes := ParseBodyOfCode(index)
		if !bodyParseResult.GotResult {
			return ParseResult{
				GotResult: false,
				Reason:    WrapStr("Couldn't parse body of if-statement", bodyParseResult.Reason),
			}
		}
		index += bodyParseResult.TokensConsumed
		saveBody(bodyParseResult, bodyNodes)

		for {
			if GetKind(index) == TokenKind_Else {
				index++
				if GetKind(index) == TokenKind_If {
					index++

					anotherConditionParseResult := ParseExpression(index, true)
					if anotherConditionParseResult.GotResult {
						index += anotherConditionParseResult.TokensConsumed
						pruneStructIfInvoked(&anotherConditionParseResult, &index)
						saveCondition(anotherConditionParseResult)
						if GetKind(index) != TokenKind_LeftCurlyBrace {
							return ParseResult{
								GotResult: false,
								Reason:    WrapStr("Couldn't find '{' after else-if condition", anotherConditionParseResult.Reason),
							}
						}
					} else {
						return ParseResult{
							GotResult: false,
							Reason:    WrapStr("Failed to parse condition for else-if", anotherConditionParseResult.Reason),
						}
					}
				}

				if GetKind(index) == TokenKind_LeftCurlyBrace {
					anotherBodyParseResult, bodyNodes := ParseBodyOfCode(index)
					if bodyParseResult.GotResult {
						index += anotherBodyParseResult.TokensConsumed
						saveBody(anotherBodyParseResult, bodyNodes)
					} else {
						return ParseResult{
							GotResult: false,
							Reason:    WrapStr("Failed to parse an if-statement body", anotherBodyParseResult.Reason),
						}
					}
				} else {
					return ParseResult{
						GotResult: false,
						Reason:    fmt.Sprintf("Unexpected token after 'else' keyword, '%s'. Expected '{' or 'if'.", GetToken(index).Data),
					}
				}
			} else {
				break
			}
		}

		return ParseResult{
			GotResult: true,
			Node: AstNode{
				Kind: AstKind_IfStatement,
				Data: AstData_IfStatement{
					Conditions: conditions[:numConditions],
					Bodies:     bodies[:numBodies],
				},
			},
			TokensConsumed: index - oldIndex,
		}
	}

	ParseBodyOfCode = func(index int) (ParseResult, []AstNode) {
		startIndex := index

		if GetKind(index) != TokenKind_LeftCurlyBrace {
			return ParseResult{
				GotResult:  false,
				Error:      errors.New("Incomplete script definition"),
				LineNumber: GetToken(startIndex).LineNumber,
				Reason:     "First token in body of code wasn't '{'",
			}, []AstNode{}
		}
		index++

		var bodyNodes AstNodeBuffer
		for {
			if ptrace {
				fmt.Fprintf(os.Stderr, "[ptrace] body idx=%d kind=%v data=%q line=%d\n", index, GetKind(index), GetToken(index).Data, GetToken(index).LineNumber)
			}
			if GetKind(index) == TokenKind_OutOfRange {
				return ParseResult{
					GotResult:  true,
					Error:      errors.New("Incomplete script definition"),
					LineNumber: GetToken(startIndex).LineNumber,
				}, []AstNode{}
				break
			} else if GetKind(index) == TokenKind_RightCurlyBrace {
				index++
				break
			} else if GetKind(index) == TokenKind_RightParenthesis {
				return ParseResult{
					GotResult:  true,
					Error:      errors.New("Unnecessary parenthesis )"),
					LineNumber: GetToken(index).LineNumber,
				}, []AstNode{}
			} else if parseResult := ParseNewLine(index); parseResult.GotResult {
				bodyNodes.MaybeSave(parseResult)
				index += parseResult.TokensConsumed
			} else if parseResult := ParseBreak(index); parseResult.GotResult {
				bodyNodes.MaybeSave(parseResult)
				index += parseResult.TokensConsumed
			} else if parseResult := ParseReturn(index); parseResult.GotResult {
				bodyNodes.MaybeSave(parseResult)
				index += parseResult.TokensConsumed
			} else if parseResult := ParseSwitch(index); parseResult.GotResult {
				bodyNodes.MaybeSave(parseResult)
				index += parseResult.TokensConsumed
			} else if parseResult := ParseIfStatement(index); parseResult.GotResult {
				bodyNodes.MaybeSave(parseResult)
				index += parseResult.TokensConsumed
			} else if parseResult := ParseWhileLoop(index); parseResult.GotResult {
				bodyNodes.MaybeSave(parseResult)
				index += parseResult.TokensConsumed
			} else if parseResult := ParseRepeatLoop(index); parseResult.GotResult {
				bodyNodes.MaybeSave(parseResult)
				index += parseResult.TokensConsumed
			} else if parseResult := ParseComment(index); parseResult.GotResult {
				bodyNodes.MaybeSave(parseResult)
				index += parseResult.TokensConsumed
			} else if parseResult := ParseAssignment(index, true); parseResult.GotResult {
				if parseResult.Error != nil {
					return parseResult, []AstNode{}
				}
				bodyNodes.MaybeSave(parseResult)
				index += parseResult.TokensConsumed
			} else if parseResult := ParseExpression(index, true); parseResult.GotResult {
				if parseResult.Error != nil {
					return parseResult, []AstNode{}
				}
				bodyNodes.MaybeSave(parseResult)
				index += parseResult.TokensConsumed
			} else {
				// No parser matched and index did not advance; returning an error
				// here prevents an infinite loop on unrecognised/unsupported tokens.
				return ParseResult{
					GotResult:  true,
					Error:      errors.New("Unrecognised token in body of code"),
					LineNumber: GetToken(index).LineNumber,
					Reason:     TokensNotRecognisedError(parser.Tokens[index:], "a script body node"),
				}, []AstNode{}
			}
		}

		if (index - startIndex) < 2 {
			return ParseResult{
				GotResult: false,
				Reason:    "Didn't parse enough tokens to be a body of code",
			}, []AstNode{}
		}

		return ParseResult{
			GotResult:      true,
			TokensConsumed: 2 + bodyNodes.TokensConsumed,
		}, bodyNodes.Nodes
	}

	ParseRandom = func(index int) ParseResult {
		oldIndex := index

		if GetKind(index) != TokenKind_Random && GetKind(index) != TokenKind_Random2 && GetKind(index) != TokenKind_Random3 {
			return ParseResult{
				GotResult: false,
				Reason:    "First token in 'random' wasn't 'random'",
			}
		}
		isNoRepeat := GetKind(index) == TokenKind_Random2
		isRandom3 := GetKind(index) == TokenKind_Random3
		index++

		// Optional non-canonical branch-longjump delta: an integer between the
		// `random` keyword and `{` (the lexer folds a leading `-` into the literal).
		// 0/absent = canonical (jumps target the random's structural end).
		longJumpDelta := 0
		if GetKind(index) == TokenKind_Integer {
			longJumpDelta, _ = strconv.Atoi(GetToken(index).Data)
			index++
		}

		if GetKind(index) != TokenKind_LeftCurlyBrace {
			return ParseResult{
				GotResult: false,
				Reason:    "Second token in 'random' wasn't '{'",
			}
		}
		index++

		// A newline right after `{` encodes the branch-0 newline (0x01 between the
		// offset table and the first branch). Per-random; preserved for byte-identity.
		branch0Newline := GetKind(index) == TokenKind_NewLine

		var branchWeights []AstNode
		var branches [][]AstNode
		numBranches := 0
		saveBranch := func(weight AstNode, branch []AstNode) {
			branchWeights = append(branchWeights, weight)
			branches = append(branches, branch)
			numBranches++
		}

		for {
			for {
				if GetKind(index) == TokenKind_NewLine ||
					GetKind(index) == TokenKind_SingleLineComment ||
					GetKind(index) == TokenKind_MultiLineComment {
					index++
				} else {
					break
				}
			}

			if GetKind(index) == TokenKind_RightCurlyBrace {
				index++
				break
			}

			integerParseResult := ParseInteger(index)
			if !integerParseResult.GotResult {
				return ParseResult{
					GotResult: false,
					Reason:    WrapStr("Failed to parse weight for branch", integerParseResult.Reason),
				}
			}
			index += integerParseResult.TokensConsumed

			bodyParseResult, bodyNodes := ParseBodyOfCode(index)
			if !bodyParseResult.GotResult {
				return ParseResult{
					GotResult: false,
					Reason:    WrapStr("Failed to parse body for branch", integerParseResult.Reason),
				}
			}
			index += bodyParseResult.TokensConsumed

			saveBranch(integerParseResult.Node, bodyNodes)
		}

		return ParseResult{
			GotResult: true,
			Node: AstNode{
				Kind: AstKind_Random,
				Data: AstData_Random{
					BranchWeights:  branchWeights[:numBranches],
					Branches:       branches[:numBranches],
					IsNoRepeat:     isNoRepeat,
					IsRandom3:      isRandom3,
					Branch0Newline: branch0Newline,
					LongJumpDelta:  longJumpDelta,
				},
			},
			TokensConsumed: index - oldIndex,
		}
	}

	ParseInvocation = func(index int) ParseResult {
		oldIndex := index

		// The invoked script name is usually a checksum/identifier, but THUG2 also
		// invokes a script stored in a LOCAL variable: `<LoadFunction> <args>`
		// (bytecode 0x2D 0x16<name> <args>). Accept a `<local>` reference here too.
		var scriptIdentifierNode AstNode
		if GetKind(index) == TokenKind_LeftAngleBracket {
			localResult := ParseChecksum(index) // parses `<x>` into a LocalReference
			if !localResult.GotResult || localResult.Node.Kind != AstKind_LocalReference {
				return ParseResult{GotResult: false, Reason: "Invocation local-reference name failed to parse"}
			}
			scriptIdentifierNode = localResult.Node
			index += localResult.TokensConsumed
		} else if GetKind(index) == TokenKind_Identifier ||
			GetKind(index) == TokenKind_RawChecksum ||
			GetKind(index) == TokenKind_Return /* TODO(brandon): remove this hack. (ParseReturn() just calls ParseInvocation() because the syntax so similar) */ {
			scriptIdentifierNode = AstNode{
				Kind: AstKind_Checksum,
				Data: AstData_Checksum{
					ChecksumToken: GetToken(index),
					IsRawChecksum: GetKind(index) == TokenKind_RawChecksum,
				},
			}
			index++
		} else {
			return ParseResult{
				GotResult: false,
				Reason:    "First token in invocation wasn't an identifier, checksum, or local reference",
			}
		}

		var parameterNodes AstNodeBuffer
		var tokensConsumedByEachParameterNode []int

		for { // gather parameters
			if GetKind(index) == TokenKind_BackwardSlash && GetKind(index+1) == TokenKind_NewLine {
				index += 2
			}
			// Invocation arguments may be comma-separated (e.g.
			// `Vibrate Actuator=0, Percent=1, duration=1,`, including a trailing
			// comma). THUG2 stores each comma as a 0x09 byte between the args;
			// keep them as parameter nodes so the recompile is byte-identical.
			if commaParseResult := ParseComma(index); commaParseResult.GotResult {
				parameterNodes.MaybeSave(commaParseResult)
				tokensConsumedByEachParameterNode = append(tokensConsumedByEachParameterNode, commaParseResult.TokensConsumed)
				index += commaParseResult.TokensConsumed
				continue
			}
			parameterParseResult := ParseInvocationParameter(index)
			if parameterParseResult.GotResult {
				if parameterParseResult.Error != nil {
					return parameterParseResult
				}
				parameterNodes.MaybeSave(parameterParseResult)
				tokensConsumedByEachParameterNode = append(tokensConsumedByEachParameterNode, parameterParseResult.TokensConsumed)
				index += parameterParseResult.TokensConsumed
			} else {
				break
			}
		}

		return ParseResult{
			GotResult: true,
			Node: AstNode{
				Kind: AstKind_Invocation,
				Data: AstData_Invocation{
					ScriptIdentifierNode:              scriptIdentifierNode,
					ParameterNodes:                    parameterNodes.Nodes,
					TokensConsumedByEachParameterNode: tokensConsumedByEachParameterNode,
				},
			},
			TokensConsumed: index - oldIndex,
		}
	}

	ParseInvocationParameter = func(index int) ParseResult {
		if assignmentParseResult := ParseAssignment(index, false); assignmentParseResult.GotResult {
			return assignmentParseResult
		}
		if expressionParseResult := ParseExpression(index, false); expressionParseResult.GotResult {
			return expressionParseResult
		}
		return ParseResult{
			GotResult: false,
			Reason:    TokensNotRecognisedError(parser.Tokens[index:], "a parameter"),
		}
	}

	ParseComment = func(index int) ParseResult {
		if GetKind(index) != TokenKind_SingleLineComment && GetKind(index) != TokenKind_MultiLineComment {
			return ParseResult{
				GotResult: false,
				Reason:    fmt.Sprintf("Not a comment (%+v)", GetToken(index)),
			}
		}
		return ParseResult{
			GotResult: true,
			Node: AstNode{
				Kind: AstKind_Comment,
				Data: AstData_Comment{
					CommentToken: GetToken(index),
				},
			},
			TokensConsumed: 1,
		}
	}

	ParseNewLine = func(index int) ParseResult {
		if GetKind(index) != TokenKind_NewLine {
			return ParseResult{
				GotResult: false,
				Reason:    fmt.Sprintf("Not a new-line token (%+v)", GetToken(index)),
			}
		}
		return ParseResult{
			GotResult: true,
			Node: AstNode{
				Kind: AstKind_NewLine,
				Data: AstData_Empty{},
			},
			TokensConsumed: 1,
		}
	}

	ParseBreak = func(index int) ParseResult {
		if GetKind(index) != TokenKind_Break {
			return ParseResult{
				GotResult: false,
				Reason:    fmt.Sprintf("Not a break token (%+v)", GetToken(index)),
			}
		}
		return ParseResult{
			GotResult: true,
			Node: AstNode{
				Kind: AstKind_Break,
				Data: AstData_Empty{},
			},
			TokensConsumed: 1,
		}
	}

	ParseReturn = func(index int) ParseResult {
		if GetKind(index) == TokenKind_Return {
			if invocationParseResult := ParseChecksumOrInvocation(index, true); invocationParseResult.GotResult {
				return ParseResult{
					GotResult: true,
					Node: AstNode{
						Kind: AstKind_Return,
						Data: AstData_UnaryExpression{
							Node: invocationParseResult.Node,
						},
					},
					TokensConsumed: invocationParseResult.TokensConsumed,
				}
			}
		}
		return ParseResult{
			GotResult: false,
			Reason:    "Not a return statement",
		}
	}

	ParseComma = func(index int) ParseResult {
		if GetKind(index) != TokenKind_Comma {
			return ParseResult{
				GotResult: false,
				Reason:    fmt.Sprintf("Not a comma token (%+v)", GetToken(index)),
			}
		}
		return ParseResult{
			GotResult: true,
			Node: AstNode{
				Kind: AstKind_Comma,
				Data: AstData_Empty{},
			},
			TokensConsumed: 1,
		}
	}

	GetKind = func(index int) TokenKind {
		return GetToken(index).Kind
	}

	GetToken = func(index int) Token {
		if numOfTokens := len(parser.Tokens); index >= numOfTokens {
			return Token{
				Kind:       TokenKind_OutOfRange,
				Data:       fmt.Sprintf("<index=%d,numOfTokens=%d>", index, numOfTokens),
				LineNumber: -1,
			}
		}
		return parser.Tokens[index]
	}

	parser.Result = ParseRoot()
}

type AstNodeBuffer struct {
	Nodes          []AstNode
	NumNodes       int
	TokensConsumed int
}

func (this *AstNodeBuffer) MaybeSave(parseResult ParseResult) {
	// Even if the node isn't appended, we need to count the number of tokens it consumed
	this.TokensConsumed += parseResult.TokensConsumed

	// PRESERVE consecutive newlines. We used to drop them here ("they will break
	// the roq decompiler"), but THUG2's real bytecode contains consecutive 0x01 New
	// Instruction bytes (~19 spots in AU_sfx), and they are JUMP TARGETS: random
	// branch-exit longjumps point at the second 0x01 of a double-newline (the loop-body
	// continuation after the random). Collapsing it deletes the target byte, so the
	// recompiler re-aimed those jumps ~16 bytes too far — into a Repeat opcode — making
	// load-time ambient-sound loops dispatch into garbage and FREEZE Australia on load.
	// We emit our own decompiler (not roq), which round-trips consecutive newlines, so
	// keep them verbatim for runtime-faithful output.

	this.Nodes = append(this.Nodes, parseResult.Node)
	this.NumNodes++
}

func TokensNotRecognisedError(tokens []Token, notRecognisedAs string) string {
	// This builds a human-readable diagnostic, but it is called with the WHOLE
	// remaining token slice on every FAILED parse attempt — and failed attempts
	// are routine during normal parsing (dispatch/speculation), most discarded.
	// Formatting all remaining tokens each time made parsing O(n^2) in file size
	// (large scripts like allanims/cas_skater took tens of seconds / "hung"). Cap
	// the number of tokens shown so each call is O(1).
	const maxShown = 16
	var messageBuilder strings.Builder
	messageBuilder.WriteString(fmt.Sprintf("Token stream not recognised as %s: [\n", notRecognisedAs))
	for i, token := range tokens {
		if i >= maxShown {
			messageBuilder.WriteString(fmt.Sprintf("  ... (%d more tokens)\n", len(tokens)-maxShown))
			break
		}
		messageBuilder.WriteString(fmt.Sprintf("  %+v,\n", token))
	}
	messageBuilder.WriteString("]")
	return messageBuilder.String()
}

func WrapStr(outer, inner string) string {
	return fmt.Sprintf("<%s: %s>", outer, inner)
}
