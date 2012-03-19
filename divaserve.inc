<?php

function diva_ajax_callback($d, $z, $w="256", $h="256") {
    dpm($d);
    dpm($z);
    return drupal_json_output(array('foo' => 'bar'));
}