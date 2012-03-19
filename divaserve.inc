<?php

function diva_ajax_callback() {
    
    
    
    return drupal_json_output(array('foo' => 'bar'));
}