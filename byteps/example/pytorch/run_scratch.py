import os

command = "python run_lm_finetuning.py \
    --output_dir=/n/tata_ddos_scratch/alerts/roberta-base-inca4 \
    --model_type=roberta \
    --model_name_or_path roberta-base \
    --do_train \
    --logging_steps 50 \
    --save_total_limit 3 \
    --gradient_accumulation_steps 5 \
    --train_data_file=wikitext-2-raw/wiki.train.raw \
    --do_eval \
    --eval_data_file=wikitext-2-raw/wiki.test.raw \
    --save_lr_loss \
    --evaluate_during_training \
    --save_steps 500 \
    --mlm \
    --num_train_epochs=50.0 \
    --compression_alg inca \
    --ef True \
    --quantization_levels 4 \
    --maxval 36 \
    --ofreq 32 \
    --overwrite_output_dir"


os.system(command)
