/*
Language: Twig
Requires: xml.js
Author: Luke Holder <lukemh@gmail.com>
Description: Twig is a templating language for PHP
Category: template
*/

function(hljs) {
  var PARAMS = {
    className: 'params',
    begin: '\\(', end: '\\)'
  };

  var FUNCTION_NAMES = 'attribute block constant cycle date dump include ' +
                  'max min parent random range source template_from_string';

  var FUNCTIONS = {
    className: 'function',
    beginKeywords: FUNCTION_NAMES,
    relevance: 0,
    contains: [
      PARAMS
    ]
  };

  var FILTER = {
    className: 'filter',
    begin: /\|[A-Za-z_]+:?/,
    keywords:
      'abs batch capitalize convert_encoding date date_modify default ' +
      'escape first format join json_encode keys last length lower ' +
      'merge nl2br number_format raw replace reverse round slice sort split ' +
      'striptags title trim upper url_encode',
    contains: [
      FUNCTIONS
    ]
  };

  var TAGS = 'autoescape block do embed extends filter flush for ' +
    'if import include macro sandbox set spaceless use verbatim';

  TAGS = TAGS + ' ' + TAGS.split(' ').map(function(t){return 'end' + t}).join(' ');

  return {
    aliases: ['craftcms'],
    case_insensitive: true,
    subLanguage: 'xml',
    contains: [
      hljs.COMMENT(/\{#/, /#}/),
      {
        className: 'template_tag',
        begin: /\{%/, end: /%}/,
        keywords: TAGS,
        contains: [FILTER, FUNCTIONS]
      },
      {
        className: 'variable',
        begin: /\{\{/, end: /}}/,
        contains: [FILTER, FUNCTIONS]
      }
    ]
  };
}
