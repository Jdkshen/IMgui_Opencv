# Recipe Examples

All recipes use paths relative to the repository and are resolved from the
recipe directory or a repository ancestor at load time. Runtime copies are
placed beside the executable under `x64/Release/recipes` and
`x64/ReleaseFixed/recipes`.

| Recipe | Image | Coverage |
| --- | --- | --- |
| `case_qr_clean.recipe` | `assets/images/qr_tests/qr_test.png` | Single clear QR, ZXing-cpp, ROI and pass judgement |
| `case_qr_multi.recipe` | `assets/images/qr_tests/qr_extreme_multi_mixed.png` | Multiple QR codes, rotation, perspective, duplicate filtering |
| `case_ocr.recipe` | `assets/images/ocr_product_sample.jpg` | OCR detection and recognition with PP-OCRv6 tiny |
| `case_measurement.recipe` | `assets/images/qr_tests/qr_test.png` | Point distance, line angle, circle diameter and line fitting |
| `case_pipeline.recipe` | `assets/images/qr_tests/qr_extreme_multi_mixed.png` | Edge, threshold, contour, morphology, color and line tools |
| `case_template_shape.recipe` | `assets/images/qr_tests/qr_test.png` | Template matching and shape matching using `12345_tpl0.png` and `12345_tpl1.png` |

Load one recipe from the application recipe menu. Start with
`case_qr_clean.recipe` to verify image loading, ROI binding, QR decoding,
result labels and judgement. The measurement case intentionally uses several
tool instances so each result can be distinguished by its label.
