<?php

/**
 * @file
 * Functions for performing/parsing highlighed solr searches with HOCR data.
 */

/**
 * Executes the given query, provides some defaults for highlighting.
 *
 * @param string $query
 *   The SOLR query.
 * @param int $offset
 *   The number of results to return.
 * @param int $limit
 *   The number of results to return.
 * @param array $params
 *   SOLR query parameters.
 *
 * @return object
 *   The Solr results; deserialized JSON.
 */
function islandora_ocr_highlighted_solr_search($query, $offset = 0, $limit = -1, array &$params = array()) {
  if (!module_exists('islandora_solr')) {
    return array();
  }
  $limit = $limit != -1 ? $limit : variable_get('islandora_ocr_solr_hocr_result_count', 32);
  // Remove any fields that have no values specified so the defaults can be
  // applied.
  $params = array_filter($params);
  $params += array(
    'hl' => 'true',
    'qt' => variable_get('islandora_solr_request_handler', 'standard'),
    'hl.fl' => variable_get('islandora_ocr_solr_hocr_highlighting_field', 'text_nodes_HOCR_hlt'),
    'hl.useFastVectorHighlighter' => variable_get('islandora_ocr_solr_hocr_highlighting_use_fast', TRUE) ? 'true' : 'false',
    // XXX: Could end up with a large fragment size with fielded
    // searches... Don't really see it as a major issue, though.
    // XXX: We add a bit to the fragsize, so we should get any characters
    // surrounding the fragment--on either side--to be able to account for the
    // fragment Solr selects vs what the first token may start with or the last
    // may end with in the HOCR.
    "hl.fragsize" => max((int) (drupal_strlen($query) * 1.10), 18),
    "hl.snippets" => "8",
  );
  $url = parse_url(variable_get('islandora_solr_url', 'localhost:8080/solr'));
  $solr = new Apache_Solr_Service($url['host'], $url['port'], $url['path'] . '/');
  $solr->setCreateDocuments(0);
  try {
    $results = $solr->search($query, $offset, $limit, $params);
    return json_decode($results->getRawResponse(), TRUE);
  }
  catch (Exception $e) {
    watchdog_exception('islandora_ocr', $e, 'Got an exception while searching.', array(), WATCHDOG_ERROR);
  }
}

/**
 * Maps any highlighted snippets in the solr search results to coordinate data.
 *
 * @param object $solr_results
 *   The Solr results; deserialized JSON.
 * @param array $solr_params
 *   SOLR query parameters.
 *
 * @return array
 *   An associative array containing all the relevent bounding information for
 *   highlighted search results:
 */
function islandora_ocr_map_highlighted_solr_results_to_bounding_boxes($solr_results, $solr_params) {
  $results = array();
  $params = $solr_results['responseHeader']['params'];
  $highlighted_documents = $solr_results['highlighting'];
  $map_hocr_search_result_to_bounding_box = function($o) {
    return $o['bbox'];
  };
  foreach ($highlighted_documents as $pid => $highlighted_fields) {
    $object = islandora_object_load($pid);
    if ($object && isset($object['HOCR'])) {
      $hocr = HOCR::fromDatastream($object['HOCR']);
      $results[$pid] = array(
        'page' => $hocr->getPageDimensions(),
        'snippets' => array(),
      );
      foreach ($highlighted_fields as $highlighted_field) {
        foreach ($highlighted_field as $snippet) {
          $matches = $hocr->search($snippet, array('solr' => $params));
          $results[$pid]['snippets'][$snippet] = array_map($map_hocr_search_result_to_bounding_box, $matches);
        }
      }
    }
  }
  return $results;
}
