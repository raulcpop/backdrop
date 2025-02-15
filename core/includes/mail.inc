<?php
/**
 * @file
 * API functions for processing and sending email.
 */

/**
 * Auto-detect appropriate line endings for emails.
 *
 * $settings['mail_line_endings'] will override this setting.
 */
define('MAIL_LINE_ENDINGS', isset($_SERVER['WINDIR']) || (isset($_SERVER['SERVER_SOFTWARE']) && strpos($_SERVER['SERVER_SOFTWARE'], 'Win32') !== FALSE) ? "\r\n" : "\n");

/**
 * Composes and optionally sends an email message.
 *
 * Sending an email works with defining an email template (subject, text
 * and possibly email headers) and the replacement values to use in the
 * appropriate places in the template. Processed email templates are
 * requested from hook_mail() from the module sending the email. Any module
 * can modify the composed email message array using hook_mail_alter().
 * Finally backdrop_mail_system()->mail() sends the email, which can
 * be reused if the exact same composed email is to be sent to multiple
 * recipients.
 *
 * Finding out what language to send the email with needs some consideration.
 * If you send email to a user, her preferred language should be fine, so
 * use user_preferred_language(). If you send email based on form values
 * filled on the page, there are two additional choices if you are not
 * sending the email to a user on the site. You can either use the language
 * used to generate the page ($language global variable) or the site default
 * language. See language_default(). The former is good if sending email to
 * the person filling the form, the later is good if you send email to an
 * address previously set up (like contact addresses in a contact form).
 *
 * Taking care of always using the proper language is even more important
 * when sending emails in a row to multiple users. Hook_mail() abstracts
 * whether the mail text comes from an administrator setting or is
 * static in the source code. It should also deal with common mail tokens,
 * only receiving $params which are unique to the actual email at hand.
 *
 * An example:
 *
 * @code
 *   function example_notify($accounts) {
 *     foreach ($accounts as $account) {
 *       $params['account'] = $account;
 *       // example_mail() will be called based on the first backdrop_mail() parameter.
 *       backdrop_mail('example', 'notice', $account->mail, user_preferred_language($account), $params);
 *     }
 *   }
 *
 *   function example_mail($key, &$message, $params) {
 *     $data['user'] = $params['account'];
 *     $options['language'] = $message['language'];
 *     user_mail_tokens($variables, $data, $options);
 *     switch($key) {
 *       case 'notice':
 *         // If the recipient can receive such notices by instant-message, do
 *         // not send by email.
 *         if (example_im_send($key, $message, $params)) {
 *           $message['send'] = FALSE;
 *           break;
 *         }
 *         $langcode = $message['language']->langcode;
 *         $message['subject'] = t('Notification from !site', $variables, array('langcode' => $langcode));
 *         $message['body'][] = t("Dear !username\n\nThere is new content available on the site.", $variables, array('langcode' => $langcode));
 *         break;
 *     }
 *   }
 * @endcode
 *
 * Another example, which uses backdrop_mail() to format a message for sending
 * later:
 *
 * @code
 *   $params = array('current_conditions' => $data);
 *   $to = 'user@example.com';
 *   $message = backdrop_mail('example', 'notice', $to, $language, $params, FALSE);
 *   // Only add to the spool if sending was not canceled.
 *   if ($message['send']) {
 *     example_spool_message($message);
 *   }
 * @endcode
 *
 * @param $module
 *   A module name to invoke hook_mail() on. The {$module}_mail() hook will be
 *   called to complete the $message structure which will already contain common
 *   defaults.
 * @param $key
 *   A key to identify the email sent. The final email id for email altering
 *   will be {$module}_{$key}.
 * @param $to
 *   The email address or addresses where the message will be sent to. The
 *   formatting of this string must comply with RFC 2822. Some examples are:
 *   - user@example.com
 *   - user@example.com, anotheruser@example.com
 *   - User <user@example.com>
 *   - User <user@example.com>, Another User <anotheruser@example.com>
 * @param $language
 *   Language object to use to compose the email.
 * @param $params
 *   Optional parameters to build the email.
 * @param string $reply
 *   Optional email address to be used to answer.
 * @param $send
 *   If TRUE, backdrop_mail() will call backdrop_mail_system()->mail() to deliver
 *   the message, and store the result in $message['result']. Modules
 *   implementing hook_mail_alter() may cancel sending by setting
 *   $message['send'] to FALSE.
 *
 * @return
 *   The $message array structure containing all details of the
 *   message. If already sent ($send = TRUE), then the 'result' element
 *   will contain the success indicator of the email, failure being already
 *   written to the watchdog. (Success means nothing more than the message being
 *   accepted at php-level, which still doesn't guarantee it to be delivered.)
 */
function backdrop_mail($module, $key, $to, $language, $params = array(), $reply = NULL, $send = TRUE) {
  $from = config_get('system.core', 'site_mail');
  if (empty($from))  {
    $from = ini_get('sendmail_from');
  }

  // Bundle up the variables into a structured array for altering.
  $message = array(
    'id'       => $module . '_' . $key,
    'module'   => $module,
    'key'      => $key,
    'to'       => $to,
    'from'     => $from,
    'reply-to' => isset($reply) ? $reply : $from,
    'language' => $language,
    'params'   => $params,
    'send'     => TRUE,
    'subject'  => '',
    'body'     => array()
  );

  // Build the default headers. Note that the first letter of each word is
  // always capitalized. See: https://tools.ietf.org/html/rfc5322
  $headers = array(
    'MIME-Version'              => '1.0',
    'Content-Type'              => 'text/plain; charset=UTF-8; format=flowed; delsp=yes',
    'Content-Transfer-Encoding' => '8Bit',
    'X-Mailer'                  => 'Backdrop CMS'
  );
  if ($from) {
    // To prevent email from looking like spam, the addresses in the Sender and
    // Return-Path headers should have a domain authorized to use the originating
    // SMTP server.
    $headers['From'] = $headers['Sender'] = $headers['Return-Path'] = $from;
  }
  if ($reply) {
    $headers['Reply-To'] = $reply;
  }
  $message['headers'] = $headers;

  // Build the email (get subject and body, allow additional headers) by
  // invoking hook_mail() on this module. We cannot use module_invoke() as
  // we need to have $message by reference in hook_mail().
  if (function_exists($function = $module . '_mail')) {
    $function($key, $message, $params);
  }

  // Invoke hook_mail_alter() to allow all modules to alter the resulting email.
  backdrop_alter('mail', $message);

  // Retrieve the responsible implementation for this message.
  $system = backdrop_mail_system($module, $key);

  // Format the message body.
  $message = $system->format($message);

  // Optionally send email.
  if ($send) {
    // The original caller requested sending. Sending was canceled by one or
    // more hook_mail_alter() implementations. We set 'result' to NULL, because
    // FALSE indicates an error in sending.
    if (empty($message['send'])) {
      $message['result'] = NULL;
    }
    // Sending was originally requested and was not canceled.
    else {
      $message['result'] = $system->mail($message);
      // Log errors.
      if (!$message['result']) {
        watchdog('mail', 'Error sending email (from %from to %to).', array('%from' => $message['from'], '%to' => $message['to']), WATCHDOG_ERROR);
        backdrop_set_message(t('Unable to send email.'), 'error');
      }
    }
  }

  return $message;
}

/**
 * Returns an object that implements the MailSystemInterface interface.
 *
 * Allows for one or more custom mail backends to format and send mail messages
 * composed using backdrop_mail().
 *
 * An implementation needs to implement the following methods:
 * - format: Allows to preprocess, format, and postprocess a mail
 *   message before it is passed to the sending system. By default, all messages
 *   may contain HTML and are converted to plain-text by the DefaultMailSystem
 *   implementation. For example, an alternative implementation could override
 *   the default implementation and additionally sanitize the HTML for usage in
 *   a MIME-encoded email, but still invoking the DefaultMailSystem
 *   implementation to generate an alternate plain-text version for sending.
 * - mail: Sends a message through a custom mail sending engine.
 *   By default, all messages are sent via PHP's mail() function by the
 *   DefaultMailSystem implementation.
 *
 * The selection of a particular implementation is controlled via the config
 * 'system.mail', which contains a keyed array.  The default implementation
 * is the class whose name is the value of 'default-system' key. A more specific
 * match first to key and then to module will be used in preference to the
 * default. To specify a different class for all mail sent by one module, set
 * the class name as the value for the key corresponding to the module name. To
 * specify a class for a particular message sent by one module, set the class
 * name as the value for the array key that is the message id, which is
 * "${module}_${key}".
 *
 * For example to debug all mail sent by the user module by logging it to a
 * file, you might set the variable as something like:
 *
 * @code
 * array(
 *   'default-system' => 'DefaultMailSystem',
 *   'user' => 'DevelMailLog',
 * );
 * @endcode
 *
 * Finally, a different system can be specified for a specific email ID (see
 * the $key param), such as one of the keys used by the contact module:
 *
 * @code
 * array(
 *   'default-system' => 'DefaultMailSystem',
 *   'user' => 'DevelMailLog',
 *   'contact_page_autoreply' => 'BackdropDevNullMailSend',
 * );
 * @endcode
 *
 * Other possible uses for system include a mail-sending class that actually
 * sends (or duplicates) each message to SMS, Twitter, instant message, etc, or
 * a class that queues up a large number of messages for more efficient bulk
 * sending or for sending via a remote gateway so as to reduce the load
 * on the local server.
 *
 * @param $module
 *   The module name which was used by backdrop_mail() to invoke hook_mail().
 * @param $key
 *   A key to identify the email sent. The final email ID for the email
 *   alter hook in backdrop_mail() would have been {$module}_{$key}.
 *
 * @return MailSystemInterface
 *
 * @throws Exception
 */
function backdrop_mail_system($module, $key) {
  $instances = &backdrop_static(__FUNCTION__, array());

  $id = $module . '_' . $key;

  $config = config('system.mail');

  // Look for overrides for the default class, starting from the most specific
  // id, and falling back to the module name.
  if ($config->get($id) !== NULL) {
    $class = $config->get($id);
  }
  elseif ($config->get($module) !== NULL) {
    $class = $config->get($module);
  }
  elseif ($config->get('default-system') !== NULL) {
    $class = $config->get('default-system');
  }
  else {
    $class = 'DefaultMailSystem';
  }

  if (empty($instances[$class])) {
    $interfaces = class_implements($class);
    if (isset($interfaces['MailSystemInterface'])) {
      $instances[$class] = new $class();
    }
    else {
      throw new Exception(t('Class %class does not implement interface %interface', array('%class' => $class, '%interface' => 'MailSystemInterface')));
    }
  }
  return $instances[$class];
}

/**
 * An interface for pluggable mail back-ends.
 */
interface MailSystemInterface {
  /**
   * Format a message composed by backdrop_mail() prior sending.
   *
   * @param $message
   *   A message array, as described in hook_mail_alter().
   *
   * @return
   *   The formatted $message.
   */
   public function format(array $message);

  /**
   * Send a message composed by backdrop_mail().
   *
   * @param $message
   *   Message array with at least the following elements:
   *   - id: A unique identifier of the email type. Examples: 'contact_user_copy',
   *     'user_password_reset'.
   *   - to: The mail address or addresses where the message will be sent to.
   *     The formatting of this string must comply with RFC 2822. Some examples:
   *     - user@example.com
   *     - user@example.com, anotheruser@example.com
   *     - User <user@example.com>
   *     - User <user@example.com>, Another User <anotheruser@example.com>
   *    - subject: Subject of the email to be sent. This must not contain any
   *      newline characters, or the mail may not be sent properly.
   *    - body: Message to be sent. Accepts both CRLF and LF line-endings.
   *      Email bodies must be wrapped. You can use backdrop_wrap_mail() for
   *      smart plain text wrapping.
   *    - headers: Associative array containing all additional mail headers not
   *      defined by one of the other parameters.  PHP's mail() looks for Cc
   *      and Bcc headers and sends the mail to addresses in these headers too.
   *
   * @return
   *   TRUE if the mail was successfully accepted for delivery, otherwise FALSE.
   */
   public function mail(array $message);
}

/**
 * Performs format=flowed soft wrapping for mail (RFC 3676).
 *
 * We use delsp=yes wrapping, but only break non-spaced languages when
 * absolutely necessary to avoid compatibility issues.
 *
 * We deliberately use LF rather than CRLF, see backdrop_mail().
 *
 * @param string $text
 *   The plain text to process.
 * @param string $indent (optional)
 *   A string to indent the text with. Only '>' characters are repeated on
 *   subsequent wrapped lines. Others are replaced by spaces.
 *
 * @return string
 *   The content of the email as a string with formatting applied.
 */
function backdrop_wrap_mail($text, $indent = '') {
  // Convert CRLF into LF.
  $text = str_replace("\r", '', $text);
  // See if soft-wrapping is allowed.
  $clean_indent = _backdrop_html_to_text_clean($indent);
  $soft = strpos($clean_indent, ' ') === FALSE;
  // Check if the string has line breaks.
  if (strpos($text, "\n") !== FALSE) {
    // Remove trailing spaces to make existing breaks hard, but leave signature
    // marker untouched (RFC 3676, Section 4.3).
    $text = preg_replace('/(?(?<!^--) +\n|  +\n)/m', "\n", $text);
    // Wrap each line at the needed width.
    $lines = explode("\n", $text);
    array_walk($lines, '_backdrop_wrap_mail_line', array('soft' => $soft, 'length' => strlen($indent)));
    $text = implode("\n", $lines);
  }
  else {
    // Wrap this line.
    _backdrop_wrap_mail_line($text, 0, array('soft' => $soft, 'length' => strlen($indent)));
  }
  // Empty lines with nothing but spaces.
  $text = preg_replace('/^ +\n/m', "\n", $text);
  // Space-stuff special lines.
  $text = preg_replace('/^(>| |From)/m', ' $1', $text);
  // Apply indentation. We only include non-'>' indentation on the first line.
  $text = $indent . substr(preg_replace('/^/m', $clean_indent, $text), strlen($indent));

  return $text;
}

/**
 * Transforms an HTML string into plain text, preserving its structure.
 *
 * The output will be suitable for use as 'format=flowed; delsp=yes' text
 * (RFC 3676) and can be passed directly to backdrop_mail() for sending.
 *
 * We deliberately use LF rather than CRLF, see backdrop_mail().
 *
 * This function provides suitable alternatives for the following tags:
 * <a> <em> <i> <strong> <b> <br> <p> <blockquote> <ul> <ol> <li> <dl> <dt>
 * <dd> <h1> <h2> <h3> <h4> <h5> <h6> <hr>
 *
 * @param $string
 *   The string to be transformed.
 * @param $allowed_tags (optional)
 *   If supplied, a list of tags that will be transformed. If omitted, all
 *   all supported tags are transformed.
 *
 * @return
 *   The transformed string.
 */
function backdrop_html_to_text($string, $allowed_tags = NULL) {
  // Cache list of supported tags.
  static $supported_tags;
  if (empty($supported_tags)) {
    $supported_tags = array('a', 'em', 'i', 'strong', 'b', 'br', 'p', 'blockquote', 'ul', 'ol', 'li', 'dl', 'dt', 'dd', 'h1', 'h2', 'h3', 'h4', 'h5', 'h6', 'hr');
  }

  // Make sure only supported tags are kept.
  $allowed_tags = isset($allowed_tags) ? array_intersect($supported_tags, $allowed_tags) : $supported_tags;

  // Make sure tags, entities and attributes are well-formed and properly nested.
  $string = _filter_htmlcorrector(filter_xss($string, $allowed_tags));

  // Apply inline styles.
  $string = preg_replace('!</?(em|i)((?> +)[^>]*)?>!i', '/', $string);
  $string = preg_replace('!</?(strong|b)((?> +)[^>]*)?>!i', '*', $string);

  // Replace inline <a> tags with the text of link and a footnote.
  // 'See <a href="https://backdropcms.org">the Backdrop CMS site</a>' becomes
  // 'See the Backdrop CMS site [1]' with the URL included as a footnote.
  _backdrop_html_to_mail_urls(NULL, TRUE);
  $pattern = '@(<a[^>]+?href="([^"]*)"[^>]*?>(.+?)</a>)@i';
  $string = preg_replace_callback($pattern, '_backdrop_html_to_mail_urls', $string);
  $urls = _backdrop_html_to_mail_urls();
  $footnotes = '';
  if (count($urls)) {
    $footnotes .= "\n";
    for ($i = 0, $max = count($urls); $i < $max; $i++) {
      $footnotes .= '[' . ($i + 1) . '] ' . $urls[$i] . "\n";
    }
  }

  // Split tags from text.
  $split = preg_split('/<([^>]+?)>/', $string, -1, PREG_SPLIT_DELIM_CAPTURE);
  // Note: PHP ensures the array consists of alternating delimiters and literals
  // and begins and ends with a literal (inserting $null as required).

  $tag = FALSE; // Odd/even counter (tag or no tag)
  $casing = NULL; // Case conversion function
  $output = '';
  $indent = array(); // All current indentation string chunks
  $lists = array(); // Array of counters for opened lists
  foreach ($split as $value) {
    $chunk = NULL; // Holds a string ready to be formatted and output.

    // Process HTML tags (but don't output any literally).
    if ($tag) {
      list($tagname) = explode(' ', strtolower($value), 2);
      switch ($tagname) {
        // List counters
        case 'ul':
          array_unshift($lists, '*');
          break;
        case 'ol':
          array_unshift($lists, 1);
          break;
        case '/ul':
        case '/ol':
          array_shift($lists);
          $chunk = ''; // Ensure blank new-line.
          break;

        // Quotation/list markers, non-fancy headers
        case 'blockquote':
          // Format=flowed indentation cannot be mixed with lists.
          $indent[] = count($lists) ? ' "' : '>';
          break;
        case 'li':
          $indent[] = isset($lists[0]) && is_numeric($lists[0]) ? ' ' . $lists[0]++ . ') ' : ' * ';
          break;
        case 'dd':
          $indent[] = '    ';
          break;
        case 'h3':
          $indent[] = '.... ';
          break;
        case 'h4':
          $indent[] = '.. ';
          break;
        case '/blockquote':
          if (count($lists)) {
            // Append closing quote for inline quotes (immediately).
            $output = rtrim($output, "> \n") . "\"\n";
            $chunk = ''; // Ensure blank new-line.
          }
          // Fall-through
        case '/li':
        case '/dd':
          array_pop($indent);
          break;
        case '/h3':
        case '/h4':
          array_pop($indent);
        case '/h5':
        case '/h6':
          $chunk = ''; // Ensure blank new-line.
          break;

        // Fancy headers
        case 'h1':
          $indent[] = '======== ';
          $casing = 'backdrop_strtoupper';
          break;
        case 'h2':
          $indent[] = '-------- ';
          $casing = 'backdrop_strtoupper';
          break;
        case '/h1':
        case '/h2':
          $casing = NULL;
          // Pad the line with dashes.
          $output = _backdrop_html_to_text_pad($output, ($tagname == '/h1') ? '=' : '-', ' ');
          array_pop($indent);
          $chunk = ''; // Ensure blank new-line.
          break;

        // Horizontal rulers
        case 'hr':
          // Insert immediately.
          $output .= backdrop_wrap_mail('', implode('', $indent)) . "\n";
          $output = _backdrop_html_to_text_pad($output, '-');
          break;

        // Paragraphs and definition lists
        case '/p':
        case '/dl':
          $chunk = ''; // Ensure blank new-line.
          break;
      }
    }
    // Process blocks of text.
    else {
      // Convert inline HTML text to plain text; not removing line-breaks or
      // white-space, since that breaks newlines when sanitizing plain-text.
      $value = trim(decode_entities($value));
      if (backdrop_strlen($value)) {
        $chunk = $value;
      }
    }

    // See if there is something waiting to be output.
    if (isset($chunk)) {
      // Apply any necessary case conversion.
      if (isset($casing)) {
        $chunk = $casing($chunk);
      }
      // Format it and apply the current indentation.
      $output .= backdrop_wrap_mail($chunk, implode('', $indent)) . MAIL_LINE_ENDINGS;
      // Remove non-quotation markers from indentation.
      $indent = array_map('_backdrop_html_to_text_clean', $indent);
    }

    $tag = !$tag;
  }

  return $output . $footnotes;
}

/**
 * Wraps words on a single line.
 *
 * Callback for array_walk() within backdrop_wrap_mail().
 */
function _backdrop_wrap_mail_line(&$line, $key, $values) {
  // Use soft-breaks only for purely quoted or unindented text.
  $line = wordwrap($line, 77 - $values['length'], $values['soft'] ? "  \n" : "\n");
  // Break really long words at the maximum width allowed.
  $line = wordwrap($line, 996 - $values['length'], $values['soft'] ? " \n" : "\n", TRUE);
}

/**
 * Keeps track of URLs and replaces them with placeholder tokens.
 *
 * Callback for preg_replace_callback() within backdrop_html_to_text().
 */
function _backdrop_html_to_mail_urls($match = NULL, $reset = FALSE) {
  global $base_url, $base_path;
  static $urls = array(), $regexp;

  if ($reset) {
    // Reset internal URL list.
    $urls = array();
  }
  else {
    if (empty($regexp)) {
      $regexp = '@^' . preg_quote($base_path, '@') . '@';
    }
    if ($match) {
      list(, , $url, $label) = $match;
      // Ensure all URLs are absolute.
      $urls[] = strpos($url, '://') ? $url : preg_replace($regexp, $base_url . '/', $url);
      return $label . ' [' . count($urls) . ']';
    }
  }
  return $urls;
}

/**
 * Replaces non-quotation markers from a given piece of indentation with spaces.
 *
 * Callback for array_map() within backdrop_html_to_text().
 */
function _backdrop_html_to_text_clean($indent) {
  return preg_replace('/[^>]/', ' ', $indent);
}

/**
 * Pads the last line with the given character.
 *
 * @see backdrop_html_to_text()
 */
function _backdrop_html_to_text_pad($text, $pad, $prefix = '') {
  // Remove last line break.
  $text = substr($text, 0, -1);
  // Calculate needed padding space and add it.
  if (($p = strrpos($text, "\n")) === FALSE) {
    $p = -1;
  }
  $n = max(0, 79 - (strlen($text) - $p) - strlen($prefix));
  // Add prefix and padding, and restore linebreak.
  return $text . $prefix . str_repeat($pad, $n) . "\n";
}
