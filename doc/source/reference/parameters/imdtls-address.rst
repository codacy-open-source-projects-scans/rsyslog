.. _param-imdtls-address:
.. _imdtls.parameter.input.address:

Address
=======

.. index::
   single: imdtls; address
   single: address

.. summary-start

Listens for DTLS syslog messages on the specified local IP address.
.. summary-end

This parameter applies to :doc:`../../configuration/modules/imdtls`.

:Name: address
:Scope: input
:Type: word
:Default: none
:Required?: no
:Introduced: v8.2402.0

Description
-----------
Specifies the IP address on which the imdtls module will listen for incoming
syslog messages. By default the module listens on all available network
connections.

Input usage
-----------
.. _imdtls.parameter.input.address-usage:

.. code-block:: rsyslog

   module(load="imdtls")
   input(type="imdtls" address="192.0.2.1")

See also
--------
See also :doc:`../../configuration/modules/imdtls`.
