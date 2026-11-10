#pragma once

typedef struct orpheus_builtin_biquad_bank_History {
    float z1[32], z2[32], x1[32], x2[32], y1[32], y2[32];
} orpheus_builtin_biquad_bank_History;

typedef struct orpheus_builtin_biquad_bank_Stage {
    float coefs[5];
    orpheus_builtin_biquad_bank_History history;
} orpheus_builtin_biquad_bank_Stage;

typedef struct orpheus_builtin_biquad_bank_AlgorithmState {
    orpheus_builtin_biquad_bank_Stage stage0, stage1;
} orpheus_builtin_biquad_bank_AlgorithmState;
