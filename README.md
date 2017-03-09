# libese

Document last updated: 13 Jan 2017

## Introduction

libese provides a minimal transport wrapper for communicating with
embedded secure elements. Embedded secure elements typically adhere
to smart card standards whose translation is not always smooth when
migrated to an always connected bus, like SPI.  The interfaces
exposed by libese should enable higher level "terminal" implementations
to be written on top and/or a service which provides a similar
interface.

Behind the interface, libese should help smooth over the differences
between eSEs and smart cards use in the hardware adapter
implementations. Additionally, a T=1 implementation is supplied,
as it appears to be the most common wire transport for these chips.

## Usage

(TBD: See tools/ and example/)

## Supported backends

At present, only sample backends and a Linux SPIdev driven NXP
developer board are supported.

