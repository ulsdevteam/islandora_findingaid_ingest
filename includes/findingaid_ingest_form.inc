<?php

// XML Transformations
define('TRANSFORM_STYLESHEET', dirname(__FILE__).'/../xsl/MARC21slim2MODS3-5.xsl');
define('TRANSFORM_MODS2DC_STYLESHEET', dirname(__FILE__).'/../../islandora_xml_forms/builder/transforms/upitt_mods_to_dc.xsl');

/**
 * @file
 * Form to upload and ingest a finding aids object from the front-end.
 */

/**
 * Form building function.
 */
function islandora_findingaid_ingest_form($form, &$form_state) {
  $sites = variable_get('islandora_memberofsite_sites');
  $sites_arr = explode("\r\n", $sites);
  foreach ($sites_arr as $site_rdf_and_url) {
    @list($site, $site_url) = explode("|", $site_rdf_and_url);
    if ($site) {
      $sites_options[$site] = $site;
    }
  }

  $form = array();
  $form['ead'] = array(
    '#type' => 'file',
    '#name' => 'files[ead]',
    '#title' => t('EAD file'),
    '#upload_location' => 'public://',
    '#upload_validators' => array(
      'file_validate_extensions' => array('xml'),
    ),
    '#default_value' => 'dummy',
  );
  $form['marc'] = array(
    '#type' => 'file',
    '#title' => t('MARC file'),
    '#upload_location' => 'public://',
    '#upload_validators' => array(
      'file_validate_extensions' => array('xml'),
    ),
    '#default_value' => 'dummy',
  );
  $form['namespace'] = array(
    '#type' => 'textfield',
    '#title' => 'Namespace',
    '#description' => 'Islandora namespace for ingested Finding Aids object.',
    '#default_value' => 'pitt',
    '#required' => TRUE,
  );
  $form['sites'] = array(
    '#type' => 'checkboxes',
    '#title' => 'Sites',
    '#options' => $sites_options,
  );

  $form['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Ingest'),
  );

  return $form;
}

/**
 * Form submit handler.
 *
 */
function islandora_findingaid_ingest_form_submit($form, $form_state) {
  $selected_sites = array();
  foreach ($form_state['values']['sites'] as $site_option) {
    if ($site_option) {
      $selected_sites[] = $site_option;
    }
  }
  if (count($selected_sites) < 1) {
    drupal_set_message('This Finding Aids object is not going to be associated with any sites.');
  }
  $ead_temp_file = $_FILES['files']['tmp_name']['ead'];
  $marc_temp_file = $_FILES['files']['tmp_name']['marc'];
  $namespace = $form_state['values']['namespace'];
  $uploaded_ead_filename = $_FILES['files']['name']['ead'];
  $uploaded_marc_filename = $_FILES['files']['name']['marc'];

  islandora_findingaid_ingest_ingest_findingaid($namespace, $ead_temp_file, 
      $uploaded_ead_filename, $marc_temp_file, $uploaded_marc_filename,
      $selected_sites);
}

function islandora_findingaid_ingest_form_validate(&$elements, &$form_state) {
  $ead_temp_file = (array_key_exists('ead', $_FILES['files']['tmp_name'])) ? $_FILES['files']['tmp_name']['ead'] : '';
  $marc_temp_file = (array_key_exists('marc', $_FILES['files']['tmp_name'])) ? $_FILES['files']['tmp_name']['marc'] : '';
  $namespace = $form_state['values']['namespace'];
  if(!$ead_temp_file) {
    form_set_error('ead', t('The EAD upload is required'));
  }
  else {
    if (!islandora_findingaid_ingest_validate_ead($ead_temp_file, $namespace)) {
      form_set_error('ead', t('The uploaded EAD file was not valid'));
    }
  }
  if(!$marc_temp_file) {
    form_set_error('marc', t('The MARC upload is required'));
  }
  else {
    if (!islandora_findingaid_ingest_validate_marc($marc_temp_file)) {
      form_set_error('marc', t('The uploaded MARC file was not valid'));
    }
  }
}

function islandora_findingaid_ingest_ingest_findingaid($namespace, $ead_temp_file,
    $uploaded_ead_filename, $marc_temp_file, $uploaded_marc_filename, $selected_sites) {
  drupal_set_message(l(t('Ingest another Finding Aids object'), 'admin/islandora/findingaid-ingest'));
  $doc0 = new DOMDocument();
  if (@$doc0->load($ead_temp_file)) {
    $doc_xml = $doc0->saveXML();
  }

  module_load_include('module', 'islandora', 'islandora');
  $islandora_root = islandora_object_load('islandora:root');
  $repository = $islandora_root->repository;

  $ead_id = islandora_findingaid_ingest_get_ead_id($doc0);
  $id = rtrim($namespace, ":") . ':' . $ead_id;
  $object = $repository->constructObject($id);

  // Get the title from the MARC file
  $title = islandora_findingaid_ingest_get_xpath_nodeValue($doc_xml);
  $object->label = ($title) ? $title : $ead_id;
  // Setting the object's models value should create a RELS-EXT
  $object->models = 'islandora:findingAidCModel';

  // These site mappings are based on the ead filename.
  islandora_findingaid_ingest_add_site_mappings($object, $selected_sites);

  $dsid = 'EAD';
  $datastream = $object->constructDatastream($dsid);
  // update existing or set new EAD datastream
  $datastream->label = $uploaded_ead_filename;
  $datastream->mimeType = 'application/xml';
  $datastream->setContentFromFile($ead_temp_file);
  $object->ingestDatastream($datastream);

  $mods_filename = islandora_findingaid_ingest_doMODSTransform($marc_temp_file, $ead_id);
  $dsid = 'MODS';
  $datastream = $object->constructDatastream($dsid);
  // update existing or set new MODS datastream
  $datastream->label = 'MODS Record';
  $datastream->mimeType = 'text/xml';
  $datastream->setContentFromFile($mods_filename);
  $object->ingestDatastream($datastream);

  $mods_file = $datastream->content;
  @unlink($mods_filename);
  $dsid = 'MARC';
  $datastream = $object->constructDatastream($dsid);
  // update existing or set new MARC datastream
  $datastream->label = $uploaded_marc_filename;
  $datastream->mimeType = 'application/xml';
  $datastream->setContentFromFile($marc_temp_file);
  $object->ingestDatastream($datastream);

  // This will update the DC record by transforming the current MODS xml.
  $mods_file = $object['MODS']->content;
  if ($mods_file == '') {
    echo "PID = " . $id . " did not exist<br>";
  }
  else {
    islandora_findingaid_ingest_doDC($object, $mods_file);
  }
  try {
    // error_log('before ingestObject call');
    // error_log('state = "' . $object->state . '"');
    // error_log(print_r($object, true));
    $repository->ingestObject($object);
    // error_log('loaded object:');

    $obj = islandora_object_load($id);
    $obj->state = 'A';
    // error_log(print_r($obj, true));
    drupal_goto('islandora/object/' . $id);
  } catch (Exception $e) {
    drupal_set_message($e->getMessage(), 'error');
  }
}

function islandora_findingaid_ingest_validate_ead($ead_temp_file, $namespace) {
  // Schema check
  $doc0 = new DOMDocument();
  if (!@$doc0->load($ead_temp_file) || (!@$doc0->schemaValidate(dirname(__FILE__) .'/../schema/ead_explicit_xsd.xsd'))) {
    drupal_set_message('ERROR: Schema did not validate for the uploaded EAD file.');
    return FALSE;
  }
  $ead_id = islandora_findingaid_ingest_get_ead_id($doc0);
  if (!$ead_id) {
    drupal_set_message('ERROR: The eadid value was not found in the uploaded EAD file.');
    return FALSE;
  }
  else {
    $id = rtrim($namespace, ":") . ':' . $ead_id;
    $object = islandora_object_load($id);
    if (is_object($object)) {
      drupal_set_message('Found eadid = "' . $ead_id . '" in the uploaded EAD file, but ' .
          'the object "' . l($id, 'islandora/object/' . $id) . '" already exists.  It ' . 
          'must be deleted manually if this EAD file is to be ingested.  To replace ' . 
          'the EAD datastream for that object, ' .
          l('click here', 'islandora/object/' . $id . '/manage/datastreams') . '.', 'error');
    }
  }
  return TRUE;
}

function islandora_findingaid_ingest_validate_marc($marc_temp_file) {
  $marc_contents = file_get_contents($marc_temp_file);
  return ($marc_contents <> '');
}

/**
 * This will run MARC to MODS transformation and save resultant MODS
 * to a temporary file. This also needs to set the
 *   Date:mods_originInfo_type_display_dateOther_s, and
 *   Depositor: mods_name_depositor_namePart_ms
 * so that the it appear for the search results item.
 *
 * Returns the filename for the new MODS file.
 */
function islandora_findingaid_ingest_doMODSTransform($marc, $ead_id) {
  $marc_file = file_get_contents($marc);
  // Get the DC by transforming from MODS.
  $new_MODS = ($marc_file) ? islandora_findingaid_ingest_runXslTransform(
            array(
              'xsl' => TRANSFORM_STYLESHEET,
              'input' => $marc_file,
            )
          ) : '';
  $new_MODS = islandora_findingaid_ingest_inject_eadid($new_MODS, $ead_id);

  $filename = tempnam("/tmp", "MODS_xml_derived_");
  // This file must be deleted in the process function that called this.
  file_put_contents($filename, $new_MODS);
  return $filename;
}

// Mostly COPIED from islandora_batch/includes/islandora_scan_batch.inc.
/**
 * Helper function to transform the MODS to get dc.
 */
function islandora_findingaid_ingest_doDC($object, $mods_content) {
  $dsid = 'DC';
  $dc_datastream = $object->constructDatastream($dsid);
  $dc_datastream->versionable = TRUE;
  $dc_datastream->mimetype = 'application/xml';
  $dc_datastream->label = 'DC Record';

  // Get the DC by transforming from MODS.
  if ($mods_content) {
    $new_dc = islandora_findingaid_ingest_runXslTransform(
            array(
              'xsl' => TRANSFORM_MODS2DC_STYLESHEET,
              'input' => $mods_content,
            )
          );
  }

  if (isset($new_dc)) {
    $dc_datastream->setContentFromString($new_dc);
  }
  echo '<a href="http://gamera.library.pitt.edu/islandora/object/' . $object->id . '">' . $object->label . '</a><br>';
  $object->ingestDatastream($dc_datastream);
}

// HORRIBLE hack
function islandora_findingaid_ingest_inject_eadid($new_MODS, $ead_id) {
  $node_partial = '<identifier type="pitt">';
  $node_full = $node_partial . $ead_id . "</identifier>";
  if (strstr($new_MODS, $node_partial)) {
    return $new_MODS;
  } else {
    return str_replace("</mods>", $node_full."</mods>", $new_MODS);
  }
}

// COPIED directly from islandora_batch/includes/islandora_scan_batch.inc.
/**
  * Run an XSLT, and return the results.
  *
  * @param array $info
  *   An associative array of parameters, containing:
  *   - input: The input XML in a string.
  *   - xsl: The path to an XSLT file.
  *   - php_functions: Either a string containing one or an array containing
  *     any number of functions to register with the XSLT processor.
  *
  * @return string
  *   The transformed XML, as a string.
  */
function islandora_findingaid_ingest_runXslTransform($info) {
  $xsl = new DOMDocument();
  $xsl->load($info['xsl']);
  $input = new DOMDocument();
  $input->loadXML($info['input']);

  $processor = new XSLTProcessor();
  $processor->importStylesheet($xsl);

  if (isset($info['php_functions'])) {
    $processor->registerPHPFunctions($info['php_functions']);
  }

  // XXX: Suppressing warnings regarding unregistered prefixes.
  return $processor->transformToXML($input);
}


function islandora_findingaid_ingest_get_ead_id($doc) {
  $nodeValue = NULL;
  $xpath = new DOMXPath($doc);
  $xpath->registerNamespace('d', 'urn:isbn:1-931666-22-9');

  $query = '//d:eadid';
  $results = $xpath->query($query);
  foreach ($results as $result) {
    $nodeValue = trim($result->nodeValue);
  }
  return $nodeValue;
}

function islandora_findingaid_ingest_get_xpath_nodeValue($doc_xml) {
  $doc = new DOMDocument();
  if (!$doc->loadXML($doc_xml)) {
    die('in islandora_findingaid_ingest_get_xpath_nodeValue, could not load XML - ' . htmlspecialchars(substr($doc_xml, 0, 99)) . '...');
    return '';
  }
  $nodeValue = $aisValue = NULL;
  $xpath = new DOMXPath($doc);
  $xpath->registerNamespace('d', 'urn:isbn:1-931666-22-9');
  $results = $xpath->query('//d:filedesc/d:titlestmt/d:titleproper');
  // the value coming from the XML usually has a bunch of extra spaces and potential line feeds
  foreach ($results as $result) {
    $nodeValue = str_replace(array("\t", "\r", "\n"), "", trim($result->nodeValue));
  }
  while (strstr($nodeValue, "  ")) {
    $nodeValue = str_replace("  ", " ", $nodeValue);
  }

  $ais_number_results = $xpath->query('/d:ead/d:eadheader/d:filedesc/d:titlestmt/d:titleproper/d:num');
  foreach ($ais_number_results as $ais_number_result) {
    $aisValue = str_replace(array("\t", "\r", "\n"), "", trim($ais_number_result->nodeValue));
  }

  // if there is an AIS number, do a string-replace to inject a comma.
  if (!is_null($aisValue)) {
    $nodeValue = str_replace(" " . $aisValue,
          ', ' . $aisValue,
          $nodeValue);
  }
  return $nodeValue;
}

function islandora_findingaid_ingest_add_site_mappings($object, $selected_sites) {
  $memberofsite_namespace = variable_get('islandora_memberofsite_namespace', 'http://digital.library.pitt.edu/ontology/relations#');
  foreach ($selected_sites as $site) {
    $object->relationships->add($memberofsite_namespace, 'isMemberOfSite', $site);
  }
}
