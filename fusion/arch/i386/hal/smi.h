#include <config.h>

#ifdef CONFIG_SMI_WORKAROUND

#ifdef __cplusplus
extern "C" {
#endif

void rthal_smi_disable(void);

void rthal_smi_restore(void);

void rthal_smi_init(void);

#ifdef __cplusplus
}
#endif

#else /* ! CONFIG_SMI_WORKAROUND */

#define rthal_smi_disable()

#define rthal_smi_disable()

#define rthal_smi_init()

#endif /* CONFIG_SMI_WORKAROUND */
