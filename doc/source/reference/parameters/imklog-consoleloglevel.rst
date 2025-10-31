.. _param-imklog-consoleloglevel:
.. _imklog.parameter.module.consoleloglevel:

ConsoleLogLevel
================

.. index::
   single: imklog; ConsoleLogLevel
   single: ConsoleLogLevel

.. summary-start

Filters kernel console messages, printing only those with a severity up to the configured level.

.. summary-end

This parameter applies to :doc:`../../configuration/modules/imklog`.

:Name: ConsoleLogLevel
:Scope: module
:Type: integer
:Default: -1
:Required?: no
:Introduced: at least 5.x, possibly earlier

Description
-----------
Sets the console log level. If specified, only messages with up to the
specified level are printed to the console. The default is -1, which
means that the current settings are not modified. To get this behavior,
do not specify ``consoleLogLevel`` in the configuration file. Note that
this is a global parameter. Each time it is changed, the previous
definition is reset. The active setting will be the last one defined when
imklog actually starts processing. To avoid unexpected behavior, it is
recommended to specify this directive only once.

**Linux only**, ignored on other platforms (but may be specified).

Module usage
------------
.. _param-imklog-module-consoleloglevel:
.. _imklog.parameter.module.consoleloglevel-usage:

.. code-block:: rsyslog

   module(load="imklog" consoleLogLevel="4")

Legacy names (for reference)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Historic names/directives for compatibility. Do not use in new configs.

.. _imklog.parameter.legacy.klogconsoleloglevel:

- $klogConsoleLogLevel — maps to ConsoleLogLevel (status: legacy)

.. index::
   single: imklog; $klogConsoleLogLevel
   single: $klogConsoleLogLevel

See also
--------
:doc:`../../configuration/modules/imklog`
