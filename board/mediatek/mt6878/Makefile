# SPDX-License-Identifier:	GPL-2.0

obj-y	+= mt6878_tetris.o
obj-$(CONFIG_TETRIS_SCP_HANDOFF_INVENTORY) += tetris_scp_handoff.o
obj-$(CONFIG_TETRIS_SCP_CRYPTO) += tetris_scp_crypto.o
obj-$(CONFIG_TETRIS_SCP_PREPARE_DIAGNOSTIC) += tetris_scp_prepare.o
obj-$(CONFIG_TETRIS_SCP_TCM_DIAGNOSTIC) += tetris_scp_tcm.o
obj-$(CONFIG_TETRIS_SCP_SECURITY) += tetris_scp_security.o tetris_scp_fields.asn1.o
$(obj)/tetris_scp_security.o: $(obj)/tetris_scp_fields.asn1.h
$(obj)/tetris_scp_fields.asn1.o: $(obj)/tetris_scp_fields.asn1.c $(obj)/tetris_scp_fields.asn1.h
ccflags-$(CONFIG_TETRIS_SCP_SECURITY) += -I$(obj)
