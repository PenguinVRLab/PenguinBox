/* Support for generating ACPI TPM tables
 *
 * Copyright (C) 2018 IBM, Corp.
 * Copyright (C) 2018 Red Hat Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/acpi/tpm.h"

void tpm_build_ppi_acpi(TPMIf *tpm, Aml *dev)
{
    Aml *method, *field, *ifctx, *ifctx2, *ifctx3, *func_mask,
        *not_implemented, *pak, *tpm2, *tpm3, *pprm, *pprq, *zero, *one;

    if (!object_property_get_bool(OBJECT(tpm), "ppi", &error_abort)) {
        return;
    }

    zero = aml_int(0);
    one = aml_int(1);
    func_mask = aml_int(TPM_PPI_FUNC_MASK);
    not_implemented = aml_int(TPM_PPI_FUNC_NOT_IMPLEMENTED);

    aml_append(dev,
               aml_operation_region("TPP2", AML_SYSTEM_MEMORY,
                                    aml_int(TPM_PPI_ADDR_BASE + 0x100),
                                    0x5A));
    field = aml_field("TPP2", AML_ANY_ACC, AML_NOLOCK, AML_PRESERVE);
    aml_append(field, aml_named_field("PPIN", 8));
    aml_append(field, aml_named_field("PPIP", 32));
    aml_append(field, aml_named_field("PPRP", 32));
    aml_append(field, aml_named_field("PPRQ", 32));
    aml_append(field, aml_named_field("PPRM", 32));
    aml_append(field, aml_named_field("LPPR", 32));
    aml_append(dev, field);
    pprq = aml_name("PPRQ");
    pprm = aml_name("PPRM");

    aml_append(dev,
               aml_operation_region(
                   "TPP3", AML_SYSTEM_MEMORY,
                   aml_int(TPM_PPI_ADDR_BASE +
                           0x15a ),
                           0x1));
    field = aml_field("TPP3", AML_BYTE_ACC, AML_NOLOCK, AML_PRESERVE);
    aml_append(field, aml_named_field("MOVV", 8));
    aml_append(dev, field);

    method = aml_method("TPFN", 1, AML_SERIALIZED);
    {
        Aml *op = aml_arg(0);
        ifctx = aml_if(aml_lgreater_equal(op, aml_int(0x100)));
        {
            aml_append(ifctx, aml_return(zero));
        }
        aml_append(method, ifctx);

        aml_append(method,
            aml_operation_region("TPP1", AML_SYSTEM_MEMORY,
                aml_add(aml_int(TPM_PPI_ADDR_BASE), op, NULL), 0x1));
        field = aml_field("TPP1", AML_BYTE_ACC, AML_NOLOCK, AML_PRESERVE);
        aml_append(field, aml_named_field("TPPF", 8));
        aml_append(method, field);
        aml_append(method, aml_return(aml_name("TPPF")));
    }
    aml_append(dev, method);

    pak = aml_package(2);
    aml_append(pak, zero);
    aml_append(pak, zero);
    aml_append(dev, aml_name_decl("TPM2", pak));
    tpm2 = aml_name("TPM2");

    pak = aml_package(3);
    aml_append(pak, zero);
    aml_append(pak, zero);
    aml_append(pak, zero);
    aml_append(dev, aml_name_decl("TPM3", pak));
    tpm3 = aml_name("TPM3");

    method = aml_method("_DSM", 4, AML_SERIALIZED);
    {
        uint8_t zerobyte[1] = { 0 };
        Aml *function, *arguments, *rev, *op, *op_arg, *op_flags, *uuid;

        uuid = aml_arg(0);
        rev = aml_arg(1);
        function = aml_arg(2);
        arguments = aml_arg(3);
        op = aml_local(0);
        op_flags = aml_local(1);

        ifctx = aml_if(
            aml_equal(uuid,
                      aml_touuid("3DDDFAA6-361B-4EB4-A424-8D10089D1653")));
        {
            ifctx2 = aml_if(aml_equal(function, zero));
            {
                uint8_t byte_list[2] = { 0xff, 0x01 };

                aml_append(ifctx2,
                           aml_return(aml_buffer(sizeof(byte_list),
                                                 byte_list)));
            }
            aml_append(ifctx, ifctx2);

            ifctx2 = aml_if(aml_equal(function, one));
            {
                aml_append(ifctx2, aml_return(aml_string("1.3")));
            }
            aml_append(ifctx, ifctx2);

            ifctx2 = aml_if(aml_equal(function, aml_int(2)));
            {
                aml_append(ifctx2,
                           aml_store(aml_derefof(aml_index(arguments,
                                                           zero)), op));

                aml_append(ifctx2,
                           aml_store(aml_call1("TPFN", op), op_flags));

                ifctx3 = aml_if(
                    aml_equal(
                        aml_and(op_flags, func_mask, NULL),
                        not_implemented));
                {
                    aml_append(ifctx3, aml_return(one));
                }
                aml_append(ifctx2, ifctx3);

                aml_append(ifctx2, aml_store(op, pprq));
                aml_append(ifctx2, aml_store(zero, pprm));
                aml_append(ifctx2, aml_return(zero));
            }
            aml_append(ifctx, ifctx2);

            ifctx2 = aml_if(aml_equal(function, aml_int(3)));
            {
                ifctx3 = aml_if(aml_equal(rev, one));
                {
                    aml_append(ifctx3,
                               aml_store(pprq, aml_index(tpm2, one)));
                    aml_append(ifctx3, aml_return(tpm2));
                }
                aml_append(ifctx2, ifctx3);

                ifctx3 = aml_if(aml_equal(rev, aml_int(2)));
                {
                    aml_append(ifctx3,
                               aml_store(pprq, aml_index(tpm3, one)));
                    aml_append(ifctx3,
                               aml_store(pprm, aml_index(tpm3, aml_int(2))));
                    aml_append(ifctx3, aml_return(tpm3));
                }
                aml_append(ifctx2, ifctx3);
            }
            aml_append(ifctx, ifctx2);

            ifctx2 = aml_if(aml_equal(function, aml_int(4)));
            {
                aml_append(ifctx2, aml_return(aml_int(2)));
            }
            aml_append(ifctx, ifctx2);

            ifctx2 = aml_if(aml_equal(function, aml_int(5)));
            {
                aml_append(ifctx2,
                           aml_store(aml_name("LPPR"),
                                     aml_index(tpm3, one)));
                aml_append(ifctx2,
                           aml_store(aml_name("PPRP"),
                                     aml_index(tpm3, aml_int(2))));
                aml_append(ifctx2, aml_return(tpm3));
            }
            aml_append(ifctx, ifctx2);

            ifctx2 = aml_if(aml_equal(function, aml_int(6)));
            {
                aml_append(ifctx2, aml_return(aml_int(3)));
            }
            aml_append(ifctx, ifctx2);

            ifctx2 = aml_if(aml_equal(function, aml_int(7)));
            {
                aml_append(ifctx2, aml_store(aml_derefof(aml_index(arguments,
                                                                   zero)),
                                             op));

                aml_append(ifctx2, aml_store(aml_call1("TPFN", op),
                                             op_flags));
                ifctx3 = aml_if(
                    aml_equal(
                        aml_and(op_flags, func_mask, NULL),
                        not_implemented));
                {
                    aml_append(ifctx3, aml_return(one));
                }
                aml_append(ifctx2, ifctx3);

                ifctx3 = aml_if(
                    aml_equal(
                        aml_and(op_flags, func_mask, NULL),
                        aml_int(TPM_PPI_FUNC_BLOCKED)));
                {
                    aml_append(ifctx3, aml_return(aml_int(3)));
                }
                aml_append(ifctx2, ifctx3);

                ifctx3 = aml_if(aml_equal(rev, one));
                {
                    aml_append(ifctx3, aml_store(op, pprq));
                    aml_append(ifctx3, aml_store(zero, pprm));
                }
                aml_append(ifctx2, ifctx3);

                ifctx3 = aml_if(aml_equal(rev, aml_int(2)));
                {
                    op_arg = aml_derefof(aml_index(arguments, one));
                    aml_append(ifctx3, aml_store(op, pprq));
                    aml_append(ifctx3, aml_store(op_arg, pprm));
                }
                aml_append(ifctx2, ifctx3);
                aml_append(ifctx2, aml_return(zero));
            }
            aml_append(ifctx, ifctx2);

            ifctx2 = aml_if(aml_equal(function, aml_int(8)));
            {
                aml_append(ifctx2,
                           aml_store(aml_derefof(aml_index(arguments,
                                                           zero)),
                                     op));

                aml_append(ifctx2, aml_store(aml_call1("TPFN", op),
                                             op_flags));
                aml_append(ifctx2,
                           aml_return(
                               aml_and(op_flags, func_mask, NULL)));
            }
            aml_append(ifctx, ifctx2);

            aml_append(ifctx, aml_return(aml_buffer(1, zerobyte)));
        }
        aml_append(method, ifctx);

        ifctx = aml_if(
            aml_equal(uuid,
                      aml_touuid("376054ED-CC13-4675-901C-4756D7F2D45D")));
        {
            ifctx2 = aml_if(aml_equal(function, zero));
            {
                uint8_t byte_list[1] = { 0x03 };

                aml_append(ifctx2,
                           aml_return(aml_buffer(sizeof(byte_list),
                                                 byte_list)));
            }
            aml_append(ifctx, ifctx2);

            ifctx2 = aml_if(aml_equal(function, one));
            {
                aml_append(ifctx2,
                           aml_store(aml_derefof(aml_index(arguments, zero)),
                                     op));
                {
                    aml_append(ifctx2, aml_store(op, aml_name("MOVV")));

                    aml_append(ifctx2, aml_return(zero));
                }
            }
            aml_append(ifctx, ifctx2);
        }
        aml_append(method, ifctx);
    }
    aml_append(dev, method);
}
