# Fire/Smoke ESP-DL Integration Notes

This firmware expects the converted fire/smoke detector to be available as a
`.espdl` file on the indoor board's microSD card:

- expected runtime path: `/sdcard/models/p4/lab_fire_smoke.espdl`
- default firmware backend: `ESP-DL + microSD`
- fallback if missing: placeholder heuristic / mock hazard output

## 1. Convert `best.onnx` to `.espdl`

The handoff package already includes the ONNX export at:

- `../lab_fire_smoke_model_handoff_v1-20260521T114351Z-3-001/lab_fire_smoke_model_handoff_v1/model/best.onnx`

Project helper:

```bash
python3 firmware/tools/convert_fire_smoke_onnx_to_espdl.py
```

This wrapper checks the expected input and reminds you where the generated
artifact should go.

If you already have an esp-ppq quantization script or notebook, point the
wrapper at it:

```bash
python3 firmware/tools/convert_fire_smoke_onnx_to_espdl.py \
  --quant-script /path/to/your_quantize_script.py \
  --calib-dir /path/to/calibration_images
```

Recommended model assumptions for this project:

- target: `esp32p4`
- input shape: `1,3,320,320`
- classes:
  - `0 = fire`
  - `1 = smoke`

## 2. Copy the artifact to microSD

After conversion, copy the resulting `.espdl` file to:

```text
/sdcard/models/p4/lab_fire_smoke.espdl
```

The indoor firmware will try to load that file at boot.

## 3. Firmware behavior

`esp_indoor/components/hazard_infer/` now supports an ESP-DL runtime path.

At startup it:

1. checks whether the `.espdl` file exists on microSD
2. loads the model through ESP-DL if available
3. runs fire/smoke detection directly on the latest RGB565 camera frame
4. falls back to the previous placeholder/mock path if load or inference fails

## 4. Tunable menuconfig options

See `LabGuard indoor hazard inference` in menuconfig for:

- model path
- score threshold
- NMS threshold
- smoke/flame alarm thresholds
- top-k detections
- internal RAM budget preference

## 5. Important limitation

The current postprocessor assumes the exported model behaves like a standard
YOLO detection head whose single output tensor contains:

- box center x/y
- width/height
- class confidences

If your final esp-ppq export changes output ordering or splits outputs into
multiple tensors, `hazard_infer.cpp` postprocessing will need a follow-up
adjustment.
