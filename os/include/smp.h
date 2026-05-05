#pragma once

#include <stdint.h>

/* SMP 초기화: MP 테이블로 코어 수 감지, 3개 이상이면 코어 1에 보조 커널 구동 */
void smp_init(void);
