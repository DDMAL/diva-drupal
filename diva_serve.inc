<?php
/**
 * @file
 * A module for working with diva.js
 */

function diva_ajax_callback() {
    $divaid = preg_replace('/[^-a-zA-Z0-9_]/', '', $_GET['d']);
    $zoom = preg_replace('/[^0-9]/', '', $_GET['z']);

    $cachekey = $divaid . "-" . $zoom;

    if ($cached = cache_get($cachekey, 'cache_diva')) {
        return drupal_json_output($cached->data);
    } 
    else {
        diva_cache_images($divaid);
        $cached = cache_get($cachekey, 'cache_diva');
        return drupal_json_output($cached->data);
    }
}