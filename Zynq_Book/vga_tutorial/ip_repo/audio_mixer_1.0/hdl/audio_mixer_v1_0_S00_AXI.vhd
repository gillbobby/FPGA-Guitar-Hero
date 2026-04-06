library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity audio_mixer_v1_0_S00_AXI is
    generic (
        C_S_AXI_DATA_WIDTH : integer := 32;
        C_S_AXI_ADDR_WIDTH : integer := 5
    );
    port (
        buttons : in std_logic_vector(4 downto 0);

        S_AXI_ACLK    : in  std_logic;
        S_AXI_ARESETN : in  std_logic;
        S_AXI_AWADDR  : in  std_logic_vector(C_S_AXI_ADDR_WIDTH-1 downto 0);
        S_AXI_AWPROT  : in  std_logic_vector(2 downto 0);
        S_AXI_AWVALID : in  std_logic;
        S_AXI_AWREADY : out std_logic;
        S_AXI_WDATA   : in  std_logic_vector(C_S_AXI_DATA_WIDTH-1 downto 0);
        S_AXI_WSTRB   : in  std_logic_vector((C_S_AXI_DATA_WIDTH/8)-1 downto 0);
        S_AXI_WVALID  : in  std_logic;
        S_AXI_WREADY  : out std_logic;
        S_AXI_BRESP   : out std_logic_vector(1 downto 0);
        S_AXI_BVALID  : out std_logic;
        S_AXI_BREADY  : in  std_logic;
        S_AXI_ARADDR  : in  std_logic_vector(C_S_AXI_ADDR_WIDTH-1 downto 0);
        S_AXI_ARPROT  : in  std_logic_vector(2 downto 0);
        S_AXI_ARVALID : in  std_logic;
        S_AXI_ARREADY : out std_logic;
        S_AXI_RDATA   : out std_logic_vector(C_S_AXI_DATA_WIDTH-1 downto 0);
        S_AXI_RRESP   : out std_logic_vector(1 downto 0);
        S_AXI_RVALID  : out std_logic;
        S_AXI_RREADY  : in  std_logic
    );
end audio_mixer_v1_0_S00_AXI; 

architecture arch_imp of audio_mixer_v1_0_S00_AXI is

    signal axi_awaddr  : std_logic_vector(C_S_AXI_ADDR_WIDTH-1 downto 0);
    signal axi_awready : std_logic;
    signal axi_wready  : std_logic;
    signal axi_bresp   : std_logic_vector(1 downto 0);
    signal axi_bvalid  : std_logic;
    signal axi_araddr  : std_logic_vector(C_S_AXI_ADDR_WIDTH-1 downto 0);
    signal axi_arready : std_logic;
    signal axi_rdata   : std_logic_vector(C_S_AXI_DATA_WIDTH-1 downto 0);
    signal axi_rresp   : std_logic_vector(1 downto 0);
    signal axi_rvalid  : std_logic;


    -- Register map:
    --   000 = 0x00 = slv_reg0  (write) backing sample from CPU
    --   001 = 0x04 = slv_reg1  (write) vocal sample from CPU
    --   010 = 0x08 = slv_reg2  (read)  button state from hardware
    --   011 = 0x0C = slv_reg3  (read)  volume-scaled mixed output
    --   100 = 0x10 = slv_reg4  (write) volume level 0-10
    constant ADDR_LSB          : integer := (C_S_AXI_DATA_WIDTH/32) + 1;
    constant OPT_MEM_ADDR_BITS : integer := 2;

    signal slv_reg0 : std_logic_vector(C_S_AXI_DATA_WIDTH-1 downto 0);
    signal slv_reg1 : std_logic_vector(C_S_AXI_DATA_WIDTH-1 downto 0);
    signal slv_reg2 : std_logic_vector(C_S_AXI_DATA_WIDTH-1 downto 0);
    signal slv_reg3 : std_logic_vector(C_S_AXI_DATA_WIDTH-1 downto 0);
    signal slv_reg4 : std_logic_vector(C_S_AXI_DATA_WIDTH-1 downto 0);

    signal slv_reg_rden : std_logic;
    signal slv_reg_wren : std_logic;
    signal reg_data_out : std_logic_vector(C_S_AXI_DATA_WIDTH-1 downto 0);
    signal byte_index   : integer;
    signal aw_en        : std_logic;

  
    --   Stage 1: mix_raw = (reg0>>>2) + (reg1>>>1)
    --                            vol_int = clamp(reg4, 0..10)
    --   Stage 2:    product = mix_raw * vol_int
    --   Stage 3:    result  = (product * 6554) >>> 16
    --                            with saturation to signed 16-bit
    

    -- Stage 1 signals (combinational)
    signal backing_shifted : signed(16 downto 0);
    signal vocal_shifted   : signed(16 downto 0);
    signal mix_raw         : signed(16 downto 0);
    signal vol_raw         : integer range 0 to 15;
    signal vol_int         : integer range 0 to 10;

    -- Stage 2 signals 
    signal product_r       : signed(31 downto 0);

    -- Stage 3 signals
    signal final_product   : signed(47 downto 0);
    signal divided         : signed(31 downto 0);

begin

    S_AXI_AWREADY <= axi_awready;
    S_AXI_WREADY  <= axi_wready;
    S_AXI_BRESP   <= axi_bresp;
    S_AXI_BVALID  <= axi_bvalid;
    S_AXI_ARREADY <= axi_arready;
    S_AXI_RDATA   <= axi_rdata;
    S_AXI_RRESP   <= axi_rresp;
    S_AXI_RVALID  <= axi_rvalid;

    process (S_AXI_ACLK)
    begin
        if rising_edge(S_AXI_ACLK) then
            if S_AXI_ARESETN = '0' then
                axi_awready <= '0';
                aw_en <= '1';
            else
                if (axi_awready = '0' and S_AXI_AWVALID = '1' and
                    S_AXI_WVALID = '1' and aw_en = '1') then
                    axi_awready <= '1';
                    aw_en <= '0';
                elsif (S_AXI_BREADY = '1' and axi_bvalid = '1') then
                    aw_en <= '1';
                    axi_awready <= '0';
                else
                    axi_awready <= '0';
                end if;
            end if;
        end if;
    end process;

    process (S_AXI_ACLK)
    begin
        if rising_edge(S_AXI_ACLK) then
            if S_AXI_ARESETN = '0' then
                axi_awaddr <= (others => '0');
            else
                if (axi_awready = '0' and S_AXI_AWVALID = '1' and
                    S_AXI_WVALID = '1' and aw_en = '1') then
                    axi_awaddr <= S_AXI_AWADDR;
                end if;
            end if;
        end if;
    end process;


    process (S_AXI_ACLK)
    begin
        if rising_edge(S_AXI_ACLK) then
            if S_AXI_ARESETN = '0' then
                axi_wready <= '0';
            else
                if (axi_wready = '0' and S_AXI_WVALID = '1' and
                    S_AXI_AWVALID = '1' and aw_en = '1') then
                    axi_wready <= '1';
                else
                    axi_wready <= '0';
                end if;
            end if;
        end if;
    end process;


    slv_reg_wren <= axi_wready and S_AXI_WVALID and axi_awready and S_AXI_AWVALID;

    process (S_AXI_ACLK)
    variable loc_addr : std_logic_vector(OPT_MEM_ADDR_BITS downto 0);
    begin
        if rising_edge(S_AXI_ACLK) then
            if S_AXI_ARESETN = '0' then
                slv_reg0 <= (others => '0');
                slv_reg1 <= (others => '0');
                -- slv_reg2 is button input
                -- slv_reg3 is computed output 
                slv_reg4 <= x"0000000A";   -- default volume = 10 
            else
                loc_addr := axi_awaddr(ADDR_LSB + OPT_MEM_ADDR_BITS downto ADDR_LSB);
                if (slv_reg_wren = '1') then
                    case loc_addr is
                        when b"000" =>
                            -- Reg0 is backing track sample from CPU
                            for byte_index in 0 to (C_S_AXI_DATA_WIDTH/8-1) loop
                                if (S_AXI_WSTRB(byte_index) = '1') then
                                    slv_reg0(byte_index*8+7 downto byte_index*8)
                                        <= S_AXI_WDATA(byte_index*8+7 downto byte_index*8);
                                end if;
                            end loop;
                        when b"001" =>
                            -- Reg1 is vocal sample from CPU
                            for byte_index in 0 to (C_S_AXI_DATA_WIDTH/8-1) loop
                                if (S_AXI_WSTRB(byte_index) = '1') then
                                    slv_reg1(byte_index*8+7 downto byte_index*8)
                                        <= S_AXI_WDATA(byte_index*8+7 downto byte_index*8);
                                end if;
                            end loop;
                        when b"100" =>
                            -- Reg4: volume level (0-10) from CPU
                            for byte_index in 0 to (C_S_AXI_DATA_WIDTH/8-1) loop
                                if (S_AXI_WSTRB(byte_index) = '1') then
                                    slv_reg4(byte_index*8+7 downto byte_index*8)
                                        <= S_AXI_WDATA(byte_index*8+7 downto byte_index*8);
                                end if;
                            end loop;
                        when others =>
                            slv_reg0 <= slv_reg0;
                            slv_reg1 <= slv_reg1;
                            slv_reg4 <= slv_reg4;
                    end case;
                end if;
            end if;
        end if;
    end process;


    process (S_AXI_ACLK)
    begin
        if rising_edge(S_AXI_ACLK) then
            if S_AXI_ARESETN = '0' then
                axi_bvalid <= '0';
                axi_bresp  <= "00";
            else
                if (axi_awready = '1' and S_AXI_AWVALID = '1' and
                    axi_wready = '1' and S_AXI_WVALID = '1' and
                    axi_bvalid = '0') then
                    axi_bvalid <= '1';
                    axi_bresp  <= "00";
                elsif (S_AXI_BREADY = '1' and axi_bvalid = '1') then
                    axi_bvalid <= '0';
                end if;
            end if;
        end if;
    end process;


    process (S_AXI_ACLK)
    begin
        if rising_edge(S_AXI_ACLK) then
            if S_AXI_ARESETN = '0' then
                axi_arready <= '0';
                axi_araddr  <= (others => '1');
            else
                if (axi_arready = '0' and S_AXI_ARVALID = '1') then
                    axi_arready <= '1';
                    axi_araddr  <= S_AXI_ARADDR;
                else
                    axi_arready <= '0';
                end if;
            end if;
        end if;
    end process;

    process (S_AXI_ACLK)
    begin
        if rising_edge(S_AXI_ACLK) then
            if S_AXI_ARESETN = '0' then
                axi_rvalid <= '0';
                axi_rresp  <= "00";
            else
                if (axi_arready = '1' and S_AXI_ARVALID = '1' and
                    axi_rvalid = '0') then
                    axi_rvalid <= '1';
                    axi_rresp  <= "00";
                elsif (axi_rvalid = '1' and S_AXI_RREADY = '1') then
                    axi_rvalid <= '0';
                end if;
            end if;
        end if;
    end process;


    slv_reg_rden <= axi_arready and S_AXI_ARVALID and (not axi_rvalid);

    process (slv_reg0, slv_reg1, slv_reg2, slv_reg3, slv_reg4,
             axi_araddr, S_AXI_ARESETN, slv_reg_rden, buttons)
    variable loc_addr : std_logic_vector(OPT_MEM_ADDR_BITS downto 0);
    begin
        loc_addr := axi_araddr(ADDR_LSB + OPT_MEM_ADDR_BITS downto ADDR_LSB);
        case loc_addr is
            when b"000" =>
                reg_data_out <= slv_reg0;
            when b"001" =>
                reg_data_out <= slv_reg1;
            when b"010" =>
                -- Reg2 is button state from hardware pins
                reg_data_out <= (others => '0');
                reg_data_out(4 downto 0) <= buttons;
            when b"011" =>
                -- Reg3 is volume-scaled mixed output
                reg_data_out <= slv_reg3;
            when b"100" =>
                -- Reg4 holds current volume level 
                reg_data_out <= slv_reg4;
            when others =>
                reg_data_out <= (others => '0');
        end case;
    end process;

    process (S_AXI_ACLK) is
    begin
        if (rising_edge(S_AXI_ACLK)) then
            if (S_AXI_ARESETN = '0') then
                axi_rdata <= (others => '0');
            else
                if (slv_reg_rden = '1') then
                    axi_rdata <= reg_data_out;
                end if;
            end if;
        end if;
    end process;


    -- Register map used for volume scaled audio mixing process
    --   slv_reg0 = backing sample  signed 16-bit in 32-bit word
    --   slv_reg1 = vocal sample    signed 16-bit in 32-bit word
    --   slv_reg4 = volume 0-10    unsigned int in 32-bit word
    --   slv_reg3 = output          signed 16-bit, sign-extended to 32
    --   mix     = (backing >>> 2) + (vocal >>> 1)    
    --   product = mix * clamp(volume, 0-10)
    --   output  = saturate_16( (product * 6554) >>> 16 )
    -- This gives:  output = mix * volume / 10
    -- At volume=10: output = mix  (full passthrough)
    -- At volume=0:  output = 0    (silence)
    
    -- multiplying by 6554 then shift 16 trick to avoid hardware divider
    -- 6554/65536 = 0.10003, error < 0.03%.

    -- Stage 1: combinational mix and volume clamp
    backing_shifted <= resize(shift_right(signed(slv_reg0(15 downto 0)), 2), 17);
    vocal_shifted   <= resize(shift_right(signed(slv_reg1(15 downto 0)), 1), 17);
    mix_raw         <= backing_shifted + vocal_shifted;

    vol_raw <= to_integer(unsigned(slv_reg4(3 downto 0)));
    vol_int <= vol_raw when vol_raw <= 10 else 10;

    -- Stages 2 + 3: registered multiply and div by 10
    process (S_AXI_ACLK)
        variable v_product  : signed(31 downto 0);
        variable v_full     : signed(47 downto 0);
        variable v_divided  : signed(31 downto 0);
    begin
        if rising_edge(S_AXI_ACLK) then
            if S_AXI_ARESETN = '0' then
                slv_reg3 <= (others => '0');
            else
                -- Multiply mix by volume (0 to 10)
                -- mix_raw is 17 bits, vol_int max 10 -> product max ~21 bits
                v_product := resize(mix_raw * to_signed(vol_int, 5), 32);

                -- Divide by 10 using multiply-shift
                -- v_product * 6554, then arithmetic shift right 16
                v_full := v_product * to_signed(6554, 16);
                v_divided := resize(shift_right(v_full, 16), 32);

                -- Saturate to signed 16-bit and sign-extend to 32 bits
                if v_divided > 32767 then
                    slv_reg3 <= std_logic_vector(to_signed(32767, 32));
                elsif v_divided < -32768 then
                    slv_reg3 <= std_logic_vector(to_signed(-32768, 32));
                else
                    slv_reg3(15 downto 0)  <= std_logic_vector(v_divided(15 downto 0));
                    slv_reg3(31 downto 16) <= (others => v_divided(15));
                end if;
            end if;
        end if;
    end process;

end arch_imp;