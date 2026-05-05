/**
 * lafillo.h - LardOS 최적화 브라우저 엔진
 *
 * 운영체제에 최적화된 경량 HTML→텍스트 변환.
 * 메모리 최소화, 외부 의존성 없음, 커널 인라인 가능.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

/** HTTP 응답 → 평문 추출. 성공 시 0 반환. */
int lafillo_http_to_text(const char* http_resp, uint32_t resp_len, char* out, uint32_t out_cap);
