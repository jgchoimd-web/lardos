# Seed - 셀프 호스팅 컴파일러 언어

**Seed**는 BOSL 바이트코드로 컴파일되는 최소 C 스타일 언어이며, **컴파일러가 Seed로 작성되어 자기 자신을 컴파일할 수 있는** 셀프 호스팅 구조를 목표로 합니다.

## 설계 원칙

- 기존 언어(BOSL, LIL 등)와 **독립적** — bosla, lil 등은 변경하지 않음
- **Bootstrap**: seedc (C 구현)가 첫 컴파일러
- **Self-hosting**: compiler.seed를 Seed로 작성 → seedc로 컴파일 → 결과 BOSL이 Seed 소스를 컴파일

## 문법

```
프로그램    = { 전역선언 | 함수정의 }
전역선언    = "int" 식별자 [ "[" 수 "]" ] [ "=" 초기화리스트 ] ";"
함수정의    = "int" 식별자 "(" [ "int" 식별자 { "," "int" 식별자 } ] ")" 블록
블록        = "{" { 선언 | 문 } "}"
선언        = "int" 식별자 [ "[" 수 "]" ] ";"
문          = 식 ";" | "if" "(" 식 ")" 문 [ "else" 문 ]
            | "while" "(" 식 ")" 문 | "return" [ 식 ] ";"
식          = 할당 | 논리Or
논리Or      = 논리And { "||" 논리And }
논리And     = 비교 { "&&" 비교 }
비교        = 덧셈 { ("<"|"<="|">"|">="|"=="|"!=") 덧셈 }
덧셈        = 곱셈 { ("+"|"-") 곱셈 }
곱셈        = 단항 { ("*"|"/"|"%") 단항 }
단항       = "!" 단항 | "-" 단항 | 기본
기본       = 수 | 식별자 | 식별자 "[" 식 "]" | 식별자 "(" 인자 ")" | "(" 식 ")"
인자       = [ 식 { "," 식 } ]
할당       = 식별자 "=" 식 | 식별자 "[" 식 "]" "=" 식
```

## 타입

- `int` 만 사용 (32비트)
- `int arr[N]` — 고정 크기 배열
- 배열 초기화: `int x[4] = {1, 2, 3, 4};`

## 내장 함수

- `putint(int n)` — 정수 출력 (10진수 + 줄바꿈)
- `putchar(int c)` — 문자 출력 (c & 0xFF)

## 예시

```seed
int main() {
  int x;
  x = 40;
  putint(x + 2);
  return 0;
}
```

## 빌드

```bash
./lang/seed/seedc lang/seed/examples/hello.seed -o hello.bosli
```

## 셀프 호스팅 체인

1. **seedc** (C) — compiler.seed → compiler.bosli
2. **compiler.bosli** — Seed 소스 입력 → BOSL 바이트코드 출력
3. 입력 = compiler.seed 자신이면 → 출력 = compiler.bosli (자기 자신)
