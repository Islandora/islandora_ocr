<?php

/**
 * @file
 * Hooks for islandora_ocr module
 */


/**
 *
 * This hooks allows modules to define their own list of human readable names for the given tesseract abbreviation
 *
 */
function hook_get_ocr_tesseract_languages() {

    // https://github.com/tesseract-ocr/tesseract/wiki/Data-Files
    return array(
        'swa' => t('Swahili'),
        'swe' => t('Swedish'),
        'syr' => t('Syriac'),
        'tam' => t('Tamil'),
        'tel' => t('Telugu'),
        'tgk' => t('Tajik'),
        'tgl' => t('Tagalog'),
        'tha' => t('Thai'),
        'tir' => t('Tigrinya'),
        'tur' => t('Turkish'),
        'uig' => t('Uighur; Uyghur'),
        'ukr' => t('Ukrainian'),
        'urd' => t('Urdu'),
        'uzb' => t('Uzbek'),
        'uzb_cyrl' => t('Uzbek - Cyrillic'),
        'vie' => t('Vietnamese'),
        'yid' => t('Yiddish'),
    );
}
