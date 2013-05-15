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
  $output = array(); $ret = 0;
  $command = "{$executable} {$image_file} {$image_file} -l {$options['language']} 2>&1";
  exec($command, $output, $ret);
  if ($ret != '0' || !file_exists($out_file)) {
    $message = 'Tesseract failed to create OCR datastreams.<br/>Error: @ret<br/>Command: @command<br/>Output: !output';
    $variables = array(
      '@ret' => $ret,
      '@command' => $command,
      '!output' => implode('<br/>', $output),
    );
    watchdog('islandora_ocr', $message, $variables, WATCHDOG_ERROR);
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
  $output = array(); $ret = 0;
  $command = "{$executable} {$image_file} {$image_file} -l {$options['language']} hocr 2>&1";
  exec($command, $output, $ret);
  if ($ret != '0' || !file_exists($out_file)) {
    $message = 'Tesseract failed to create an HOCR datastream.<br/>Error: @ret<br/>Command: @command<br/>Output: !output';
    $variables = array(
      '@ret' => $ret,
      '@command' => $command,
      '!output' => implode('<br/>', $output),
    );
    watchdog('islandora_ocr', $message, $variables, WATCHDOG_ERROR);
    return FALSE;
  }

  if (!HOCR::isValid($out_file)) {
    $message = 'Tesseract failed to create a valid HOCR datastream. Please ensure you are running a supported version of Tesseract (3.02.02 or later).';
    $message .= '<br/>Error: @ret<br/>Command: @command<br/>Output: !output';
    $variables = array(
      '@ret' => $ret,
      '@command' => $command,
      '!output' => implode('<br/>', $output),
    );
    watchdog('islandora_ocr', $message, $variables, WATCHDOG_ERROR);
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
 * Creates and adds the OCR, HOCR datastreams.
 *
 * @param AbstractDatastream $datastream
 *   The page datastream that the derivatives will be generated for.
 * @param array $options
 *   The options for tesseract.
 *
 * @return bool
 *   TRUE on success, FALSE otherwise.
 */
function islandora_ocr_derive_datastreams(AbstractDatastream $datastream, array $options) {
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

  // XXX: Likely an incomplete list... Cursory search didn't turn up a full
  // list.
  $supported_codecs = array(
    'jpeg',
    'tiff',
    'png',
  );

  if ($depth > 8 || !in_array($codec, $supported_codecs)) {
    $c_dest = file_create_filename(drupal_basename($source_file) . ".tif", 'temporary://');

    $c_args = array(
      '-colorspace Gray',
      '-depth 8',
      '-compress None',
    );

    $old_source = $source_file;
    $source_file = islandora_ocr_imagemagick_convert($source_file, $c_dest, $c_args);
    drupal_unlink($old_source);
  }

  $ocr_file = islandora_ocr_create_ocr($source_file, $options);
  $hocr_file = islandora_ocr_create_hocr($source_file, $options);
  $ret = $ocr_file;
  $ret = $ret && islandora_ocr_update_datastream($datastream->parent, $ocr_file, 'OCR');
  $ret = $ret && $hocr_file;
  $ret = $ret && islandora_ocr_update_datastream($datastream->parent, $hocr_file, 'HOCR');

  $files = array_filter(array($source_file, $ocr_file, $hocr_file));
  array_map('file_unmanaged_delete', $files);
  return $ret;
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
    $message = 'ImageMagick failed to create a derivative.<br/>Error: @ret<br/>Command: @command<br/>Output: @output';
    $variables = array(
      '@ret' => $ret,
      '@command' => $command,
      '@output' => $output,
    );
    watchdog('islandora_ocr', $message, $variables, WATCHDOG_ERROR);
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
  if (empty($object[$dsid])) {
    $ds = $object->constructDatastream($dsid, $control_group);
    $object->ingestDatastream($ds);
  }
  else {
    $ds = $object[$dsid];
  }
  $ds->setContentFromFile($file, $copy);
  $ds->label = isset($label) ? $label : $dsid;
  if (isset($mimetype)) {
    $ds->mimetype = $mimetype;
  }
  else {
    $ds->mimetype = $mime_detector->getMimetype($file);
  }
  return TRUE;
}
