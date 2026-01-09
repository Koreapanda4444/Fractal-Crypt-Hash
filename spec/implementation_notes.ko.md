# 구현 노트(Implementation Notes)

## 설계 철학(Design Philosophy)

FCH의 핵심 아이디어는 확산(diffusion)을

- 시간(time, 라운드 반복)

에서

- 공간(space, 재귀 구조)

으로 이동시키는 것입니다.

리프 압축은 의도적으로 최소화되어 있으며,
대부분의 확산은 구조적 재결합에서 발생합니다.

---

## 근거(Rationale)

- 가변 n-way 분할은 균일한 구조 가정을 어렵게 만듭니다
- 패턴 기반 분할은 구조를 입력 내용에 결합합니다
- 순서 의존 결합은 위치 민감도를 보장합니다
- 재압축은 상태의 선형적 성장을 억제합니다

---

## 레퍼런스 구현 범위(Reference Implementation Scope)

본 구현은 다음을 우선합니다:

- 명확성(clarity)
- 결정성(determinism)
- 구조적 투명성(structural transparency)

성능과 암호학적 난이도는 부차적입니다.
