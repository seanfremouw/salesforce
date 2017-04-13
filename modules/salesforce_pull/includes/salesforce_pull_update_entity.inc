<?php

/**
 * @file
 * Salesforce pull update entity.
 */

/**
 * Update an existing entity from a salesforce object.
 */
function salesforce_pull_update_entity($mapping_object, $sf_mapping, $sf_object, $hold_exceptions) {
  $exception = FALSE;
  try {
    $entity = entity_load_single($mapping_object->entity_type, $mapping_object->entity_id);
    if ($entity === FALSE) {
      $exists = FALSE;
      $message = t('Unable to update %type entity %label from Salesforce object %sfobjectid. Entity does not exist. Mapping removed, continuing with Create instead Update.',
        array(
          '%type' => $mapping_object->entity_type,
          '%label' => $mapping_object->entity_id,
          '%sfobjectid' => $sf_object['Id'],
        ));
      watchdog('Salesforce Pull', $message, array(), WATCHDOG_NOTICE);
      salesforce_set_message($message, 'status', FALSE);
      entity_delete('salesforce_mapping_object', $mapping_object->salesforce_mapping_object_id);
    }
    else {
      // Flag this entity as having been processed. This does not persist,
      // but is used by salesforce_push to avoid duplicate processing.
      $entity->salesforce_pull = TRUE;
      $entity_updated = isset($entity->updated) ? $entity->updated : $mapping_object->entity_updated;

      $sf_object_updated = strtotime($sf_object[$sf_mapping->pull_trigger_date]);
      if ($sf_object_updated > $entity_updated) {
        $wrapper = entity_metadata_wrapper($sf_mapping->drupal_entity_type, $entity);

        // Set fields values on the Drupal entity.
        salesforce_pull_map_fields($sf_mapping->field_mappings, $wrapper, $sf_object);

        // Allow modules to react just prior to entity save.
        module_invoke_all('salesforce_pull_entity_presave', $wrapper->value(), $sf_object, $sf_mapping);

        // Update entity.
        $wrapper->save();

        // Allow modules to react to entity update.
        module_invoke_all('salesforce_pull_entity_update', $wrapper->value(), $sf_object, $sf_mapping);

        // Update mapping object.
        $mapping_object->last_sync_message = t('Retrieved updates from Salesforce');
        $mapping_object->last_sync_status = SALESFORCE_MAPPING_STATUS_SUCCESS;
        $mapping_object->entity_updated = $mapping_object->last_sync = time();
        watchdog('Salesforce Pull',
          'Updated entity %label associated with Salesforce Object ID: %sfid',
          array(
            '%label' => $wrapper->label(),
            '%sfid' => $sf_object['Id'],
          )
        );
      }
    }
  } catch (Exception $e) {
    $message = t('Failed to update entity %label %id from Salesforce object %sfobjectid. Error: @msg',
      array(
        '%label' => $mapping_object->entity_type,
        '%id' => $mapping_object->entity_id,
        '%sfobjectid' => $sf_object['Id'],
        '@msg' => $e->getMessage(),
      )
    );
    watchdog('Salesforce Pull', $message, array(), WATCHDOG_ERROR);
    salesforce_set_message($message, 'error', FALSE);
    $mapping_object->last_sync_status = SALESFORCE_MAPPING_STATUS_ERROR;
    $mapping_object->last_sync_message = t('Processing failed');
    $mapping_object->last_sync = time();
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

  return array('mapping_object' => $mapping_object, 'exception' => $exception);
}
