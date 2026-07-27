#include "speaker-embedding-model-data.h"

// The VLC backend disables speaker identification. Keep the symbols expected
// by Moonshine while omitting its 17 MB embedded diarization model.
const uint8_t speaker_embedding_model_ort_bytes[] = {0};
const size_t speaker_embedding_model_ort_byte_count = 0;
