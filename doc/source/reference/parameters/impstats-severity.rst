.. _param-impstats-severity:
.. _impstats.parameter.module.severity:

severity
========

.. index::
   single: impstats; severity
   single: severity

.. summary-start

Sets the syslog severity value that impstats assigns to emitted messages.

.. summary-end

This parameter applies to :doc:`../../configuration/modules/impstats`.

:Name: severity
:Scope: module
:Type: integer
:Default: module=6
:Required?: no
:Introduced: at least 5.x, possibly earlier

Description
-----------
The numerical syslog severity code to be used for generated messages. Default
is 6 (info). This is useful for filtering messages.

Module usage
------------
.. _impstats.parameter.module.severity-usage:

.. code-block:: rsyslog

   module(load="impstats" severity="7")

See also
--------
See also :doc:`../../configuration/modules/impstats`.
