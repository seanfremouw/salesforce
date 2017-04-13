<?php

/**
 * @file
 * Salesforce pull insert entity.
 */

/**
 * Insert a new entity and entity mapping from a salesforce object.
 */
function salesforce_pull_insert_entity($sf_mapping, $sf_object, $hold_exceptions) {
  $exception = FALSE;
  try {
    // Create entity from mapping object and field maps.
    $entity_info = entity_get_info($sf_mapping->drupal_entity_type);

    // Define values to pass to entity_create().
    $values = array();
    if (isset($entity_info['entity keys']['bundle']) &&
      !empty($entity_info['entity keys']['bundle'])
    ) {
      $values[$entity_info['entity keys']['bundle']] = $sf_mapping->drupal_bundle;

      // Because creating term via entities actually needs vid and won't be
      // fixed in Entity API (https://www.drupal.org/node/1409256).
      if (isset($values['vocabulary_machine_name'])) {
        $vocabulary = taxonomy_vocabulary_machine_name_load($values['vocabulary_machine_name']);
        $values['vid'] = $vocabulary->vid;
      }
    }
    else {
      // Not all entities will have bundle defined under entity keys,
      // e.g. the User entity.
      $values[$sf_mapping->drupal_bundle] = $sf_mapping->drupal_bundle;
    }

    // See note above about flag.
    $values['salesforce_pull'] = TRUE;

    // Create entity.
    $entity = entity_create($sf_mapping->drupal_entity_type, $values);

    // Flag this entity as having been processed. This does not persist,
    // but is used by salesforce_push to avoid duplicate processing.
    $entity->salesforce_pull = TRUE;
    $wrapper = entity_metadata_wrapper($sf_mapping->drupal_entity_type, $entity);

    salesforce_pull_map_fields($sf_mapping->field_mappings, $wrapper, $sf_object);

    // Allow modules to react just prior to entity save.
    module_invoke_all('salesforce_pull_entity_presave', $wrapper->value(), $sf_object, $sf_mapping);

    $wrapper->save();

    // Allow modules to react to entity creation.
    module_invoke_all('salesforce_pull_entity_insert', $wrapper->value(), $sf_object, $sf_mapping);

    // Update mapping object.
    $last_sync_message = t('Retrieved new record from Salesforce');
    $last_sync_status = SALESFORCE_MAPPING_STATUS_SUCCESS;
    $entity_updated = time();
  } catch (Exception $e) {
    $message = $e->getMessage() . ' ' . t('Processing failed for entity %label associated with Salesforce Object ID: %sfobjectid',
        array(
          '%label' => $wrapper->label(),
          '%sfobjectid' => $sf_object['Id'],
        )
      );
    watchdog('Salesforce Pull', $message, array(), WATCHDOG_ERROR);
    salesforce_set_message(t('There were failures processing data from Salesforce. Please check the error logs.'), 'error', FALSE);
    $last_sync_status = SALESFORCE_MAPPING_STATUS_ERROR;
    $last_sync_message = t('Processing failed for new record');
    $entity_updated = NULL;
    if (!$hold_exceptions) {
      throw $e;
    }
    if (empty($exception)) {
      $exception = $e;
    }
    else {
      $my_class = get_class($e);
      $exception = new $my_class($e->getMessage(), $e->getCode(), $exception);
    }
  }

  // If no id exists, the insert failed and we cannot create a mapping
  // object. We are left with no choice but to throw an exception.
  list($entity_id) = entity_extract_ids($sf_mapping->drupal_entity_type, $entity);
  if (!$entity_id) {
    $sf_object_id = $sf_object['Id'];
    throw new Exception("Failed to create Drupal entity when processing data from Salesforce object: $sf_object_id.");
  }

  // Create mapping object.
  $mapping_object = entity_create('salesforce_mapping_object', array(
    'salesforce_id' => $sf_object['Id'],
    'entity_type' => $sf_mapping->drupal_entity_type,
    'entity_id' => $entity_id,
    'entity_updated' => $entity_updated,
    'last_sync' => time(),
    'last_sync_message' => $last_sync_message,
    'last_sync_status' => $last_sync_status,
  ));

  watchdog('Salesforce Pull',
    'Created entity %label associated with Salesforce Object ID: %sfid',
    array(
      '%label' => $wrapper->label(),
      '%sfid' => $sf_object['Id'],
    )
  );

  return array('mapping_object' => $mapping_object, 'exception' => $exception);
}
