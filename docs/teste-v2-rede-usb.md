# Roteiro: internet no G470 pelo adaptador USB (TP-Link UE300)

**Arquivo:** `entrega/v2-g470-rede/saguios.img` (grave no pendrive como sempre).

1. Ligue o cabo de rede no adaptador e no roteador.
2. Dê boot no G470 pelo pendrive do SaguiOS.
3. Conecte o **TP-Link UE300** numa porta USB (pode ser antes ou depois do boot).
4. No Terminal:
   - `rede` deve mostrar a placa "USB: ..." e, em alguns segundos, um endereço (DHCP);
   - `ping google.com`
   - `relogio internet`
   - `navegador` e abrir a Wikipédia.
5. **Se não funcionar**, com o pendrive de dados conectado:
   ```
   registro USB > /usb/usb.txt
   registro REDE > /usb/rede.txt
   ```
   e me mande os dois arquivos (são o diário do que o SaguiOS viu).
