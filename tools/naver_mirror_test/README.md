# MU1201 Naver Map main-display mirror test

Ready-to-copy package: [`MU1201_Naver_Main_Mirror_Test_v1.zip`](MU1201_Naver_Main_Mirror_Test_v1.zip)

This is a reversible **parked-car experiment** for the already-installed
`mhi2q_kr_aug22_p1201_mu1201` Android Auto cockpit package.

It does not redirect Android Auto service 1 or remove it from the centre screen.
It copies the centre Android Auto window after GAL has decoded it, crops the app
area, and renders that copy into cockpit window 99. The independent secondary
cluster player is not started during this test, preventing two renderers from
competing for the same cockpit window. Route-arrow and HUD processing remain on.

## Safety and scope

- Perform the complete test while parked.
- Use only on the exact MU1201 profile shown above.
- The centre screen must remain on Naver Map; the cockpit copy follows it.
- No screenshot command is used. Take phone photos of both screens.
- `mib2q_aa_off` on the SD root remains the emergency off switch after reboot.
- Installation and removal preserve and restore the existing `gal_startup.sh`.

## English procedure

1. Extract the ZIP. Copy its `mod` directory to the M.I.B. SD-card root.
2. With **no marker**, run M.I.B. **Advanced settings -> Run custom script**.
   Confirm the audit reports profile `mhi2q_kr_aug22_p1201_mu1201`.
3. Create an empty file named `naver_mirror_test_install` on the SD root.
4. Run the custom script again. It must end with `INSTALLED`.
5. Remove `naver_mirror_test_install`, then reboot the MMI.
6. Connect Android Auto, open Naver Map on the centre display, start a route,
   and wait at least 30 seconds. Photograph the centre and cockpit displays.
7. Without changing the test installation, repeat once with Waze as a control.
8. Run the custom script with no marker to save logs under
   `naver-mirror-test-logs` on the SD card. Share that folder and the photos.
9. Create `naver_mirror_test_remove`, run the script, confirm `REMOVED`, delete
   the marker, and reboot the MMI. This restores the previous cluster startup.

If the centre display goes black or Android Auto fails, create `mib2q_aa_off` on
the SD root and reboot. Do not continue driving tests.

## 한국어 절차

이 패키지는 주차 상태에서만 사용하는 MU1201 네이버 지도 미러링 시험입니다.
센터 화면의 Android Auto 화면을 복사하여 계기판 window 99에 표시하며, 센터
화면 자체는 그대로 유지됩니다.

1. ZIP을 풀고 `mod` 폴더를 M.I.B. SD 카드 루트에 복사합니다.
2. 마커 파일 없이 M.I.B.의 **Advanced settings -> Run custom script**를
   실행합니다. 프로필이 `mhi2q_kr_aug22_p1201_mu1201`인지 확인합니다.
3. SD 카드 루트에 빈 파일 `naver_mirror_test_install`을 만듭니다.
4. custom script를 다시 실행하고 마지막에 `INSTALLED`가 표시되는지 확인합니다.
5. 설치 마커를 삭제한 다음 MMI를 재부팅합니다.
6. Android Auto에서 네이버 지도를 센터 화면에 열고 경로 안내를 시작한 뒤
   30초 이상 기다립니다. 센터 화면과 계기판을 사진으로 촬영합니다.
7. 같은 설정에서 Waze도 한 번 시험하여 비교 사진을 촬영합니다.
8. 마커 없이 custom script를 다시 실행합니다. SD 카드의
   `naver-mirror-test-logs` 폴더와 사진을 공유합니다.
9. SD 카드 루트에 `naver_mirror_test_remove`를 만들고 script를 실행합니다.
   `REMOVED` 확인 후 마커를 삭제하고 MMI를 재부팅합니다.

문제가 생기면 SD 카드 루트에 `mib2q_aa_off`를 만들고 MMI를 재부팅하십시오.

## Expected evidence

The useful result is one of:

- Naver centre map appears in the cockpit: main-service mirroring works.
- Waze mirrors but Naver does not: collect `sq5_galhook.log` and
  `sq5_naver_mirror.log`; this points to centre-window identification/content.
- Neither mirrors: the MU1201 GAL window ID or QNX frame format differs and the
  logs should show whether window 59 and the shared frame ring were found.
