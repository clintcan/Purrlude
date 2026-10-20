"""htdemucs_6s's network, for building it without Meta's weights: train.py --from-scratch and
tools/models/export_demucs.py --weights. Demucs 4.0.1's HTDemucs arguments for it: the network's
shape, not its weights.
"""

from fractions import Fraction

PARTS = ["drums", "bass", "other", "vocals", "guitar", "piano"]   # htdemucs_6s's order
SAMPLE_RATE = 44100

ARCHITECTURE = dict(
    sources=PARTS, audio_channels=2, samplerate=SAMPLE_RATE, segment=Fraction(39, 5), channels=48, growth=2,
    nfft=4096, wiener_iters=0, end_iters=0, wiener_residual=False, cac=True, depth=4, rewrite=True,
    multi_freqs=[], multi_freqs_depth=3, freq_emb=0.2, emb_scale=10, emb_smooth=True, kernel_size=8,
    stride=4, time_stride=2, context=1, context_enc=0, norm_starts=4, norm_groups=4, dconv_mode=3,
    dconv_depth=2, dconv_comp=8, dconv_init=1e-3, bottom_channels=0, t_layers=5, t_hidden_scale=4.0,
    t_heads=8, t_dropout=0.02, t_layer_scale=True, t_gelu=True, t_emb="sin", t_max_positions=10000,
    t_max_period=10000.0, t_weight_pos_embed=1.0, t_cape_mean_normalize=True, t_cape_augment=True,
    t_cape_glob_loc_scale=[5000.0, 1.0, 1.4], t_sin_random_shift=0, t_norm_in=True,
    t_norm_in_group=False, t_group_norm=False, t_norm_first=True, t_norm_out=True,
    t_weight_decay=0.0, t_lr=None, t_sparse_self_attn=False, t_sparse_cross_attn=False,
    t_mask_type="diag", t_mask_random_seed=42, t_sparse_attn_window=400, t_global_window=100,
    t_sparsity=0.95, t_auto_sparsity=False, t_cross_first=False, rescale=0.1)

# A tiny version, only for checking the pipeline (--small).
SMALL = dict(ARCHITECTURE, channels=8, t_layers=1, t_heads=2)
