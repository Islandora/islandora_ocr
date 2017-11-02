<?php

/**
 * @file
 * File to create derivatives with teseract
 */

/**
 * Generates a plain text OCR file, using the given options.
 *
 * @param string $image_file
 *   The absolute path to the image file, expected to be a TIFF.
 * @param array $options
 *   An associative array of options to be passed to tesseract:
 *   - language: Required, the language of the text in the image, eng, fre, etc.
 *
 * @return string
 *   The absolute path to the generated OCR file if successful, FALSE otherwise.
 */
function islandora_ocr_create_ocr($image_file, array $options) {
  $image_file = drupal_realpath($image_file);
  $out_file = "$image_file.txt";
  $executable = variable_get('islandora_ocr_tesseract', '/usr/bin/tesseract');
  $output = array();
  $ret = 0;
  $command = "{$executable} {$image_file} {$image_file} -l {$options['language']} 2>&1";
  exec($command, $output, $ret);
  if ($ret != '0' || !file_exists($out_file)) {
    $variables = array(
      '@ret' => $ret,
      '@command' => $command,
      '!output' => implode('<br/>', $output),
    );
    watchdog('islandora_ocr', 'Tesseract failed to create OCR datastreams.<br/>Error: @ret<br/>Command: @command<br/>Output: !output', $variables, WATCHDOG_ERROR);
    return FALSE;
  }
  return $out_file;
}

/**
 * Generates a XHTML HOCR file, using the given options.
 *
 * @param string $image_file
 *   The absolute path to the image file, expected to be a TIFF.
 * @param array $options
 *   An associative array of options to be passed to tesseract:
 *   - language: Required, the language of the text in the image, eng, fre, etc.
 *
 * @return string
 *   The absolute path to the generated HOCR if successful, FALSE otherwise.
 */
function islandora_ocr_create_hocr($image_file, array $options) {
  $image_file = drupal_realpath($image_file);
  $out_file = "$image_file.html";
  $executable = variable_get('islandora_ocr_tesseract', '/usr/bin/tesseract');
  $output = array();
  $ret = 0;
  $command = "{$executable} {$image_file} {$image_file} -l {$options['language']} hocr 2>&1";
  exec($command, $output, $ret);
  $version = islandora_ocr_get_tesseract_version($executable);
  if (strcmp($version, "3.02.02") > 0) {
    $base_name = drupal_basename("$image_file.hocr");
    // Add .html extension so we get correct mimetype.
    $out_file = file_unmanaged_move("$image_file.hocr", "temporary://$base_name.html", FILE_EXISTS_REPLACE);
  }
  if ($ret != '0' || empty($out_file) || !file_exists($out_file)) {
    $variables = array(
      '@ret' => $ret,
      '@command' => $command,
      '!output' => implode('<br/>', $output),
    );
    watchdog('islandora_ocr', 'Tesseract failed to create an HOCR datastream.<br/>Error: @ret<br/>Command: @command<br/>Output: !output', $variables, WATCHDOG_ERROR);
    return FALSE;
  }

  if (!HOCR::isValid($out_file)) {
    $variables = array(
      '@ret' => $ret,
      '@command' => $command,
      '!output' => implode('<br/>', $output),
    );
    watchdog('islandora_ocr',
      'Tesseract failed to create a valid HOCR datastream. Please ensure you are running a supported version of Tesseract (3.02.02 or later).<br/>Error: @ret<br/>Command: @command<br/>Output: !output',
      $variables,
      WATCHDOG_ERROR
    );
    return FALSE;
  }

  HOCR::stripDoctypeFromFile($out_file);

  return $out_file;
}

/**
 * Fetches the upload image file from Fedora, and saves it to a temp location.
 *
 * @param AbstractDatastream $datastream
 *   The object to fetch the uploaded image file from.
 *
 * @return string
 *   The file path to the temp file if successful, FALSE otherwise.
 */
function islandora_ocr_get_uploaded_file(AbstractDatastream $datastream) {
  $mime_detector = new MimeDetect();
  $ext = $mime_detector->getExtension($datastream->mimeType);
  $filename = file_create_filename(str_replace(':', '_', "{$datastream->parent->id}_{$datastream->id}.{$ext}"), 'temporary://');
  $datastream->getContent($filename);
  return $filename;
}

/**
 * Generates OCR and HOCR derivatives from a page and adds them as datastreams.
 *
 * @param AbstractDatastream $datastream
 *   The page datastream that the derivatives will be generated for.
 * @param array $options
 *   The options for tesseract.
 *
 * @return bool
 *   Returns TRUE on success and FALSE on failure.
 */
function islandora_ocr_derive_datastreams(AbstractDatastream $datastream, array $options) {
  $derivatives = islandora_ocr_generate_derivatives($datastream, $options);
  $ret = $derivatives['OCR'];
  $ret = $ret && islandora_ocr_update_datastream($datastream->parent, $derivatives['OCR'], 'OCR');
  $ret = $ret && $derivatives['HOCR'];
  $ret = $ret && islandora_ocr_update_datastream($datastream->parent, $derivatives['HOCR'], 'HOCR');
  return $ret;
}

/**
 * Generates OCR/HOCR derivatives and returns references to them.
 *
 * @param AbstractDatastream $datastream
 *   The page datastream to generate derivatives for.
 * @param array $options
 *   Options for tesseract.
 *
 * @return array
 *   An associative array containing 'OCR' and 'HOCR', paired with either the
 *   path to that derivative, or FALSE on failure.
 */
function islandora_ocr_generate_derivatives(AbstractDatastream $datastream, array $options) {
  module_load_include('inc', 'islandora_ocr', 'includes/utilities');
  $options += array(
    'language' => 'eng',
  );
  $source_file = islandora_ocr_get_uploaded_file($datastream);

  // Check the depth of the image, to see if it can be processed with
  // tesseract. If it can not, reduce the depth and grayscale it.
  $identify = islandora_ocr_get_identify();

  $file = drupal_realpath($source_file);

  $depth = intval(exec(escapeshellcmd("$identify -format \"%z\" $file")));
  $codec = strtolower(exec(escapeshellcmd("$identify -format \"%m\" $file")));
  $alpha = exec(escapeshellcmd("$identify -format \"%A\" $file"));

  // XXX: Likely an incomplete list... Cursory search didn't turn up a full
  // list.
  $supported_codecs = array(
    'jpeg',
    'tiff',
    'png',
  );
  if ($depth > 8 || !in_array($codec, $supported_codecs) || $alpha == 'True') {
    $c_dest = file_create_filename(drupal_basename($source_file) . ".tif", 'temporary://');

    $c_args = array(
      '-colorspace Gray',
      '-depth 8',
      '-compress None',
      '-alpha Off',
    );

    $old_source = $source_file;
    $source_file = islandora_ocr_imagemagick_convert($source_file, $c_dest, $c_args);
    drupal_unlink($old_source);
  }

  $return = array(
    'OCR' => islandora_ocr_create_ocr($source_file, $options),
    'HOCR' => islandora_ocr_create_hocr($source_file, $options),
  );
  drupal_unlink($source_file);
  return $return;
}

/**
 * Get an OCRable version of the given image datastream.
 *
 * @param AbstractDatastream $datastream
 *   An image datastream we want to OCR.
 *
 * @return string
 *   A path to the converted image... Delete it after you're done.
 */
function islandora_ocr_get_converted_source(AbstractDatastream $datastream) {
  module_load_include('inc', 'islandora_ocr', 'includes/utilities');
  $source_file = islandora_ocr_get_uploaded_file($datastream);

  // Check the depth of the image, to see if it can be processed with
  // tesseract. If it can not, reduce the depth and grayscale it.
  $identify = islandora_ocr_get_identify();

  $file = drupal_realpath($source_file);

  $depth = intval(exec(escapeshellcmd("$identify -format \"%z\" $file")));
  $codec = strtolower(exec(escapeshellcmd("$identify -format \"%m\" $file")));
  $alpha = exec(escapeshellcmd("$identify -format \"%A\" $file"));
  // XXX: Likely an incomplete list... Cursory search didn't turn up a full
  // list.
  $supported_codecs = array(
    'jpeg',
    'tiff',
    'png',
  );

  if ($depth > 8 || !in_array($codec, $supported_codecs) || $alpha == 'True') {
    $c_dest = file_create_filename(drupal_basename($source_file) . ".tif", 'temporary://');

    $c_args = array(
      '-colorspace Gray',
      '-depth 8',
      '-compress None',
      '-alpha Off',
    );

    $old_source = $source_file;
    $source_file = islandora_ocr_imagemagick_convert($source_file, $c_dest, $c_args);
    drupal_unlink($old_source);
  }

  return $source_file;
}

/**
 * Derives an 'OCR' or 'HOCR' datastream.
 *
 * @param string $type
 *   Either "OCR" or "HOCR".
 * @param AbstractObject $object
 *   The object on which to derive a datastream.
 * @param bool $force
 *   Whether to force derivative generation.
 *
 * @throws InvalidArgumentException
 *   If the $type is neither OCR nor HOCR.
 *
 * @return array
 *   A derivative success/fail structured array.
 */
function islandora_ocr_derive_datastream($type, AbstractObject $object, $force = FALSE) {
  // Type-dependent information.
  $dsid_map = array(
    'HOCR' => array(
      'generation_callback' => 'islandora_ocr_create_hocr',
      'flag' => ISLANDORA_OCR_GENERATE_HOCR_RELS_STATEMENT,
    ),
    'OCR' => array(
      'generation_callback' => 'islandora_ocr_create_ocr',
      'flag' => ISLANDORA_OCR_GENERATE_OCR_RELS_STATEMENT,
    ),
  );
  // Bail if $type is invalid.
  if (!isset($dsid_map[$type])) {
    throw new InvalidArgumentException(t('Invalid OCR datastream type "@type"', array(
      '@type' => $type,
    )));
  }
  $language_array = $object->relationships->get(ISLANDORA_RELS_EXT_URI, 'hasLanguage');
  $language = isset($language_array[0]['object']['value']) ? $language_array[0]['object']['value'] : 'eng';
  $generate_datastream = 'TRUE';
  // Determine if we should generate.
  $generate_value = $object->relationships->get(ISLANDORA_RELS_EXT_URI, $dsid_map[$type]['flag']);
  if ($generate_value) {
    $generate_datastream = $generate_value[0]['object']['value'];
  }
  // If an OCR flag exists with no HOCR flag, this is an old item and we should
  // use the OCR flag value.
  elseif ($type == 'HOCR') {
    $ocr_value = $object->relationships->get(ISLANDORA_RELS_EXT_URI, ISLANDORA_OCR_GENERATE_OCR_RELS_STATEMENT);
    if ($ocr_value) {
      $generate_datastream = $ocr_value[0]['object']['value'];
    }
  }
  // XXX: The 'no_ocr' is a bit misleading, as a lack of language selection
  // negates generation of HOCR as well. Leaving as-is for compatibility with
  // existing RELS.
  if ($language == 'no_ocr' || $generate_datastream == 'FALSE') {
    $to_return['success'] = TRUE;
    $to_return['messages'][] = array(
      'message' => t('Skipped adding @dsid to @id.'),
      'message_sub' => array(
        '@dsid' => $type,
        '@id' => $object->id,
      ),
      'type' => 'dsm',
      'severity' => 'status',
    );
    return $to_return;
  }

  if ($generate_datastream == 'TRUE' && ($force || !isset($object[$type]))) {
    $to_return = array(
      'success' => FALSE,
      'messages' => array(),
    );

    $datastream = $object['OBJ'];
    $file = islandora_ocr_get_converted_source($datastream);
    $derivative_file = call_user_func_array($dsid_map[$type]['generation_callback'], array($file, array('language' => $language)));
    if ($derivative_file && islandora_ocr_update_datastream($object, $derivative_file, $type)) {
      $to_return['success'] = TRUE;
      $to_return['messages'][] = array(
        'message' => t('Added @dsid to @id.'),
        'message_sub' => array(
          '@dsid' => $type,
          '@id' => $object->id,
        ),
        'type' => 'dsm',
        'severity' => 'status',
      );
    }
    else {
      $to_return['messages'][] = array(
        'message' => t('Failed to generate @dest_dsid from @src_dsid for @id.'),
        'message_sub' => array(
          '@dest_dsid' => $type,
          '@src_dsid' => 'OBJ',
          '@id' => $object->id,
        ),
        'type' => 'watchdog',
        'severity' => WATCHDOG_ERROR,
      );
    }
    file_unmanaged_delete($file);
    return $to_return;
  }
}

/**
 * Derive the OCR stream from OBJ.
 */
function islandora_ocr_derive_ocr(AbstractObject $object, $force = FALSE) {
  return islandora_ocr_derive_datastream('OCR', $object, $force);
}

/**
 * Derive the HOCR stream from OBJ.
 */
function islandora_ocr_derive_hocr(AbstractObject $object, $force = FALSE) {
  return islandora_ocr_derive_datastream('HOCR', $object, $force);
}

/**
 * Attempt to get the "identify" executable...
 *
 * Assume convert is sanely named, and that identify can be obtained from the
 * same place.
 *
 * @return string
 *   The command to run for "identify".
 */
function islandora_ocr_get_identify() {
  // Get path for convert.
  $convert = variable_get('imagemagick_convert', 'convert');

  // Replace "convert" with "identify".
  $identify = str_replace('convert', 'identify', $convert);

  return $identify;
}

/**
 * Calls imagemagick's convert command with the given arguments.
 *
 * @param string $src
 *   The source file uri to convert from.
 * @param string $dest
 *   The destination file uri to convert into.
 * @param array $args
 *   The arguements to pass to imagemagik
 *
 * @todo add better documentation for the args parameter
 *
 * @return string
 *   The destination file path if successful otherwise FALSE.
 */
function islandora_ocr_imagemagick_convert($src, $dest, $args) {
  $src = drupal_realpath($src) . '[0]';
  $dest = drupal_realpath($dest);
  $context = array(
    'source' => $src,
    'destination' => $dest,
  );
  drupal_alter('imagemagick_arguments', $args, $context);
  // To make use of ImageMagick 6's parenthetical command grouping we need to
  // make the $source image the first parameter and $dest the last.
  // See http://www.imagemagick.org/Usage/basics/#cmdline
  $command = escapeshellarg($src) . ' ' . implode(' ', $args) . ' ' . escapeshellarg($dest);
  $output = '';
  $ret = -1;
  if (_imagemagick_convert_exec($command, $output, $ret) !== TRUE) {
    $variables = array(
      '@ret' => $ret,
      '@command' => $command,
      '@output' => $output,
    );
    watchdog('islandora_ocr', 'ImageMagick failed to create a derivative.<br/>Error: @ret<br/>Command: @command<br/>Output: @output', $variables, WATCHDOG_ERROR);
    return FALSE;
  }
  return $dest;
}

/**
 * Create/Update a datastream with the given parameters.
 *
 * @param AbstractObject $object
 *   The object to update.
 * @param string $file
 *   The absolute path to the file to use as content for the datastream.
 * @param string $dsid
 *   The datastream ID.
 * @param string $label
 *   The datastream label.
 * @param string $mimetype
 *   The datastream mimetype, if none given it will be guessed from the file
 *   extension.
 * @param string $control_group
 *   The datastream control group.
 *
 * @return bool
 *   TRUE if successful, FALSE otherwise.
 */
function islandora_ocr_update_datastream(AbstractObject $object, $file, $dsid, $label = NULL, $mimetype = NULL, $control_group = 'M', $copy = TRUE) {
  $mime_detector = new MimeDetect();
  $ingest = !isset($object[$dsid]);
  if ($ingest) {
    $ds = $object->constructDatastream($dsid, $control_group);
  }
  else {
    $ds = $object[$dsid];
  }
  $ds->setContentFromFile($file, $copy);
  $label = isset($label) ? $label : $dsid;
  if ($ds->label != $label) {
    $ds->label = $label;
  }
  $mimetype = isset($mimetype) ? $mimetype : $mime_detector->getMimetype($file);
  if ($ds->mimetype != $mimetype) {
    $ds->mimetype = $mimetype;
  }
  if ($ingest) {
    $object->ingestDatastream($ds);
  }
  file_unmanaged_delete($file);
  return TRUE;
}

/**
 * Remove the generate_ocr field if present in the RELS-EXT.
 */
function islandora_ocr_remove_generate_ocr_rels_ext_statement(AbstractObject $object) {
  module_load_include('inc', 'islandora', 'includes/utilities');
  $message = filter_xss(islandora_deprecated('7.x-1.11', t('In the future, OCR-generating RELS-EXT statements should be removed together using islandora_ocr_remove_generating_rels_ext_statements().')));
  trigger_error($message, E_USER_DEPRECATED);
  islandora_ocr_remove_generating_rels_ext_statements($object, TRUE, FALSE);
}

/**
 * Set the generate_ocr relation in the RELS-EXT.
 */
function islandora_ocr_set_generate_ocr_rels_ext_statement(AbstractObject $object, $generate_ocr = TRUE) {
  module_load_include('inc', 'islandora', 'includes/utilities');
  $message = filter_xss(islandora_deprecated('7.x-1.11', t('In the future, OCR-generating RELS-EXT statements should be set together using islandora_ocr_set_generating_rels_ext_statements().')));
  trigger_error($message, E_USER_DEPRECATED);
  $generate_ocr = $generate_ocr ? 'TRUE' : 'FALSE';
  $object->relationships->add(ISLANDORA_RELS_EXT_URI, ISLANDORA_OCR_GENERATE_OCR_RELS_STATEMENT, $generate_ocr, RELS_TYPE_PLAIN_LITERAL);
}

/**
 * Helper to remove one or more OCR/HOCR-generating statements.
 *
 * @param AbstractObject $object
 *   Object to remove these statements from.
 * @param bool $generate_ocr
 *   Whether to remove the 'generate ocr' statement.
 * @param bool $generate_hocr
 *   Whether to remove the 'generate hocr' statement.
 *
 * @return array
 *   Returns an empty array to satisfy the requirements of derivative-generating
 *   callbacks so that this can be used as a derivative callback after
 *   generating OCR and/or HOCR.
 */
function islandora_ocr_remove_generating_rels_ext_statements(AbstractObject $object, $generate_ocr = TRUE, $generate_hocr = TRUE) {
  $previous_autocommit = $object->relationships->autoCommit;
  $object->relationships->autoCommit = FALSE;
  $statements = array(
    ISLANDORA_OCR_GENERATE_OCR_RELS_STATEMENT => $generate_ocr,
    ISLANDORA_OCR_GENERATE_HOCR_RELS_STATEMENT => $generate_hocr,
  );
  foreach ($statements as $predicate => $remove) {
    if ($remove) {
      $object->relationships->remove(ISLANDORA_RELS_EXT_URI, $predicate, NULL);
    }
  }
  $object->relationships->autoCommit = $previous_autocommit;
  return array();
}

/**
 * Helper to set the RELS-EXT statements used in OCR/HOCR generation.
 *
 * @param AbstractObject $object
 *   Object to set these statements to.
 * @param bool $generate_ocr
 *   The 'generate_ocr' statement to set; will be converted to a string literal.
 *   Leave NULL to ignore setting this statement.
 * @param bool $generate_hocr
 *   The 'generate_hocr' statement to set; will be converted to a string
 *   literal. Leave NULL to ignore setting this statement.
 * @param string $lang
 *   The 3-character language code to be passed to Tesseract. Leave NULL to
 *   ignore setting this statement.
 */
function islandora_ocr_set_generating_rels_ext_statements(AbstractObject $object, $generate_ocr = NULL, $generate_hocr = NULL, $lang = NULL) {
  $map_to_string_literal = function ($bool) {
    return $bool ? 'TRUE' : 'FALSE';
  };
  $statements = array(
    ISLANDORA_OCR_GENERATE_OCR_RELS_STATEMENT => $generate_ocr,
    ISLANDORA_OCR_GENERATE_HOCR_RELS_STATEMENT => $generate_hocr,
  );
  $previous_autocommit = $object->relationships->autoCommit;
  $object->relationships->autoCommit = FALSE;
  foreach ($statements as $predicate => $bool) {
    if (!is_null($bool)) {
      $object->relationships->add(ISLANDORA_RELS_EXT_URI, $predicate, $map_to_string_literal($bool), RELS_TYPE_PLAIN_LITERAL);
    }
  }
  if (!is_null($lang)) {
    $object->relationships->add(ISLANDORA_RELS_EXT_URI, 'hasLanguage', $lang, RELS_TYPE_PLAIN_LITERAL);
  }
  $object->relationships->autoCommit = $previous_autocommit;
}
